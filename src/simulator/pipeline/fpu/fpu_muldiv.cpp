#include "fpu.hpp"

// Radix-4 Booth partial product selector for a 3-bit multiplier window
uint32_t FPU::boothSelect(uint32_t M, uint32_t neg_M, uint8_t window, uint32_t shift) const
{
    uint32_t pp = 0;
    switch (window & 0b111U)
    {
    case 0b001:
    case 0b010:
        pp = M;
        break;
    case 0b011:
        pp = M << 1;
        break;
    case 0b100:
        pp = neg_M << 1;
        break;
    case 0b101:
    case 0b110:
        pp = neg_M;
        break;
    default:
        pp = 0;
        break;
    }
    return barrelShiftLeft(pp, shift);
}

// Unrolled 16x16 -> 32-bit Multiplier (Radix-4 Booth + 4-stage Wallace Tree + Kogge-Stone)
uint32_t FPU::boothWallaceKoggeMul16(uint32_t M, uint32_t Q) const
{
    uint32_t neg_M = koggeStoneAdder32(~M, 1U, false).sum;

    // Stage 1: Generate 9 parallel Radix-4 Booth partial products
    uint32_t r0 = boothSelect(M, neg_M, (Q << 1) & 0b110U, 0);
    uint32_t r1 = boothSelect(M, neg_M, (Q >> 1) & 0b111U, 2);
    uint32_t r2 = boothSelect(M, neg_M, (Q >> 3) & 0b111U, 4);
    uint32_t r3 = boothSelect(M, neg_M, (Q >> 5) & 0b111U, 6);
    uint32_t r4 = boothSelect(M, neg_M, (Q >> 7) & 0b111U, 8);
    uint32_t r5 = boothSelect(M, neg_M, (Q >> 9) & 0b111U, 10);
    uint32_t r6 = boothSelect(M, neg_M, (Q >> 11) & 0b111U, 12);
    uint32_t r7 = boothSelect(M, neg_M, (Q >> 13) & 0b111U, 14);
    uint32_t r8 = (Q & 0x8000U) ? (M << 16) : 0U; // Unsigned MSB correction row

    // Stage 2: 4-level Wallace Tree CSA reduction (9 -> 6 -> 4 -> 3 -> 2 rows)
    uint32_t s1_0, c1_0, s1_1, c1_1, s1_2, c1_2;
    carrySaveAdder(r0, r1, r2, s1_0, c1_0);
    carrySaveAdder(r3, r4, r5, s1_1, c1_1);
    carrySaveAdder(r6, r7, r8, s1_2, c1_2);

    uint32_t s2_0, c2_0, s2_1, c2_1;
    carrySaveAdder(s1_0, c1_0, s1_1, s2_0, c2_0);
    carrySaveAdder(c1_1, s1_2, c1_2, s2_1, c2_1);

    uint32_t s3_0, c3_0;
    carrySaveAdder(s2_0, c2_0, s2_1, s3_0, c3_0);

    uint32_t s4_0, c4_0;
    carrySaveAdder(s3_0, c3_0, c2_1, s4_0, c4_0);

    // Stage 3: Final Kogge-Stone parallel-prefix addition
    return koggeStoneAdder32(s4_0, c4_0, false).sum;
}

// 32x32 -> 64-bit Multiplier: decomposes into four 16x16 Booth-Wallace multipliers
void FPU::fastMul32(uint32_t a, uint32_t b, uint32_t &prod_hi, uint32_t &prod_lo) const
{
    uint32_t a0 = a & 0xFFFFU;
    uint32_t a1 = a >> 16;
    uint32_t b0 = b & 0xFFFFU;
    uint32_t b1 = b >> 16;

    // 4 partial cross-products computed via hardware Booth-Wallace-Kogge units
    uint32_t p0 = boothWallaceKoggeMul16(a0, b0);
    uint32_t p1 = boothWallaceKoggeMul16(a0, b1);
    uint32_t p2 = boothWallaceKoggeMul16(a1, b0);
    uint32_t p3 = boothWallaceKoggeMul16(a1, b1);

    // Sum middle cross-products: (p1 + p2)
    AdderResult32 mid = koggeStoneAdder32(p1, p2, false);
    uint32_t mid_lo = barrelShiftLeft(mid.sum, 16);
    uint32_t mid_hi = barrelShiftRight(mid.sum, 16) | (mid.cout ? 0x10000U : 0U);

    // Combine into lower 32-bit and upper 32-bit registers
    AdderResult32 lo_res = koggeStoneAdder32(p0, mid_lo, false);
    AdderResult32 hi_res = koggeStoneAdder32(p3, mid_hi, lo_res.cout);

    prod_lo = lo_res.sum;
    prod_hi = hi_res.sum;
}

// Multiplies two fixed-point numbers in Q2.30 format (2 integer bits, 30 fractional bits)
uint32_t FPU::mulQ30(uint32_t a, uint32_t b) const
{
    uint32_t hi = 0, lo = 0;
    fastMul32(a, b, hi, lo);
    // Result is in Q4.60; shift right by 30 bits to scale back to Q2.30
    return barrelShiftLeft(hi, 2) | barrelShiftRight(lo, 30);
}

// Floating-point Multiplication
uint32_t FPU::executeMul(uint32_t a, uint32_t b) const
{
    FloatData A(a), B(b);
    uint32_t resSign = A.sign ^ B.sign;

    // Edge case checks
    if (A.isNaN || B.isNaN)
        return IEEE754_CANONICAL_NAN;
    if ((A.isInf && B.isZero) || (A.isZero && B.isInf))
        return IEEE754_CANONICAL_NAN;
    if (A.isInf || B.isInf)
        return (resSign << 31) | IEEE754_INFINITY_MASK;
    if (A.isZero || B.isZero)
        return (resSign << 31);

    // Add exponents and subtract bias (127): Exp = ExpA + ExpB - 127
    int32_t sumExp = static_cast<int32_t>(koggeStoneAdder32(A.exp, B.exp, false).sum);
    int32_t resExp = static_cast<int32_t>(koggeStoneAdder32(sumExp, ~127U, true).sum);

    // Multiply 24-bit mantissas to produce 48-bit product
    uint32_t prod_hi = 0, prod_lo = 0;
    fastMul32(A.mant, B.mant, prod_hi, prod_lo);

    // Pack upper bits [47:16] into prod_msb, fold bottom 16 bits into sticky
    uint32_t prod_msb = barrelShiftLeft(prod_hi, 16) | barrelShiftRight(prod_lo, 16);
    bool sticky = (prod_lo & 0xFFFFU) != 0;

    uint32_t resMant;
    bool G, R, S;

    // Normalize 48-bit product: check if product overflowed to bit 47
    if (prod_msb & (1U << 31))
    {
        resMant = barrelShiftRight(prod_msb, 8);
        G = (prod_msb >> 7) & 1U;
        R = (prod_msb >> 6) & 1U;
        S = ((prod_msb & 0x3FU) != 0) || sticky;
        resExp = static_cast<int32_t>(koggeStoneAdder32(resExp, 1U, false).sum);
    }
    else
    {
        resMant = barrelShiftRight(prod_msb, 7);
        G = (prod_msb >> 6) & 1U;
        R = (prod_msb >> 5) & 1U;
        S = ((prod_msb & 0x1FU) != 0) || sticky;
    }

    return roundAndPack(resSign, resExp, resMant, G, R, S);
}

// Division: Newton-Raphson reciprocal + exact remainder correction

// One Newton-Raphson step in Q2.30: X' = X * (2 - D * X)
uint32_t FPU::newtonRaphsonStep(uint32_t D, uint32_t X) const
{
    uint32_t T = mulQ30(D, X);
    uint32_t E = koggeStoneAdder32(TWO_Q30, ~T, true).sum; // E = 2 - T
    return mulQ30(X, E);
}

// Newton-Raphson reciprocal of a 24-bit divisor mantissa.
// Returns X ~= 1/D in Q2.30, where D = divisor_mant / 2^23 lies in [1.0, 2.0).
uint32_t FPU::newtonRaphsonReciprocal24(uint32_t divisor_mant) const
{
    // Initial 4-bit lookup table for 1/D in Q2.30 format
    static const uint32_t RECIPROCAL_SEED_Q30[16] = {
        0x3E0F83E1U, 0x3A84BDAAU, 0x3757F629U, 0x34853FF4U,
        0x31F548DFU, 0x2F9D3B83U, 0x2D82D36DU, 0x2B9B4CB7U,
        0x29CE8ECEU, 0x282B4A28U, 0x26A6F576U, 0x253F69D3U,
        0x23F0FE7BU, 0x22B9B5BEU, 0x219665DFU, 0x20842120U};

    // Align the 24-bit mantissa to Q2.30 range [1.0, 2.0)
    uint32_t D = barrelShiftLeft(divisor_mant, 7);

    uint32_t X = RECIPROCAL_SEED_Q30[(D >> 26) & 0x0FU]; // ~5 bits
    X = newtonRaphsonStep(D, X);                         // ~10 bits
    X = newtonRaphsonStep(D, X);                         // ~20 bits
    X = newtonRaphsonStep(D, X);                         // ~28 bits (limited by Q2.30 truncation)
    return X;
}

uint32_t FPU::estimateQuotient(uint32_t a, uint32_t X) const
{
    uint32_t hi = 0, lo = 0;
    fastMul32(a, X, hi, lo);
    return barrelShiftLeft(hi, 5) | barrelShiftRight(lo, 27);
}

// If the remainder is negative (q too large), step q down by one: q -= 1, r += b
void FPU::quotientStepDown(uint32_t &q, uint32_t &r, uint32_t b) const
{
    bool neg = (r >> 31) & 1U;
    uint32_t q_dec = koggeStoneAdder32(q, 0xFFFFFFFFU, false).sum; // q - 1
    uint32_t r_inc = koggeStoneAdder32(r, b, false).sum;           // r + b
    q = neg ? q_dec : q;
    r = neg ? r_inc : r;
}

// If the remainder is >= b (q too small), step q up by one: q += 1, r -= b
void FPU::quotientStepUp(uint32_t &q, uint32_t &r, uint32_t b) const
{
    uint32_t r_dec = koggeStoneAdder32(r, ~b, true).sum; // r - b
    bool ge = !((r_dec >> 31) & 1U);                     // r >= b (r is non-negative here)
    uint32_t q_inc = koggeStoneAdder32(q, 1U, false).sum;
    q = ge ? q_inc : q;
    r = ge ? r_dec : r;
}

// Turns the Newton-Raphson estimate into the exact quotient floor(a * 2^26 / b).
void FPU::refineQuotient(uint32_t a, uint32_t b, uint32_t X,
                    uint32_t &q, bool &inexact) const
{
    q = estimateQuotient(a, X);

    uint32_t qb_hi = 0, qb_lo = 0;
    fastMul32(q, b, qb_hi, qb_lo);
    uint32_t r = koggeStoneAdder32(barrelShiftLeft(a, 26), ~qb_lo, true).sum; // a*2^26 - q*b

    // Fixed correction network (no loops needed in hardware: parallel compare/select)
    quotientStepDown(q, r, b); // q0 too large by 1 (r < 0)
    quotientStepUp(q, r, b);   // q0 too small by 1 (r >= b)

    inexact = (r != 0);
}

// Exact 24-bit mantissa division. q is a 26- or 27-bit quotient of a/b.
void FPU::divideMantissas(uint32_t a, uint32_t b, uint32_t &q, bool &inexact) const
{
    uint32_t X = newtonRaphsonReciprocal24(b);
    refineQuotient(a, b, X, q, inexact);
}

// Floating-point Division (correctly rounded, round-to-nearest-even)
uint32_t FPU::executeDiv(uint32_t a, uint32_t b) const
{
    FloatData A(a), B(b);
    uint32_t resSign = A.sign ^ B.sign;

    // Edge case checks
    if (A.isNaN || B.isNaN)
        return IEEE754_CANONICAL_NAN;
    if (A.isZero && B.isZero)
        return IEEE754_CANONICAL_NAN;
    if (A.isInf && B.isInf)
        return IEEE754_CANONICAL_NAN;
    if (B.isZero)
        return (resSign << 31) | IEEE754_INFINITY_MASK; // Division by zero
    if (A.isZero)
        return (resSign << 31);
    if (B.isInf)
        return (resSign << 31);
    if (A.isInf)
        return (resSign << 31) | IEEE754_INFINITY_MASK;

    // Subtract exponents and add bias (127): Exp = ExpA - ExpB + 127
    int32_t subExp = static_cast<int32_t>(koggeStoneAdder32(A.exp, ~B.exp, true).sum);
    int32_t resExp = static_cast<int32_t>(koggeStoneAdder32(subExp, 127U, false).sum);

    // Exact quotient q = floor(A.mant * 2^26 / B.mant) via Newton-Raphson + remainder correction
    uint32_t q = 0;
    bool inexact = false;
    divideMantissas(A.mant, B.mant, q, inexact);

    uint32_t resMant;
    bool G, R, S;

    // A.mant/B.mant lies in (0.5, 2): q is a 27-bit value if the ratio >= 1, otherwise 26-bit
    if (q & (1U << 26))
    {
        // Ratio in [1, 2): 24 mantissa bits | G R | S (bit 0 + remainder)
        resMant = barrelShiftRight(q, 3);
        G = (q >> 2) & 1U;
        R = (q >> 1) & 1U;
        S = ((q & 1U) != 0) || inexact;
    }
    else
    {
        // Ratio in (0.5, 1): 24 mantissa bits | G R, S comes only from the remainder
        resMant = barrelShiftRight(q, 2);
        G = (q >> 1) & 1U;
        R = q & 1U;
        S = inexact;
        resExp = static_cast<int32_t>(koggeStoneAdder32(resExp, ~1U, true).sum);
    }

    return roundAndPack(resSign, resExp, resMant, G, R, S);
}
