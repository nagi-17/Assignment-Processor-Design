// FPU floating-point addition / subtraction
#include "fpu.hpp"
#include <utility>

// Floating-point Addition / Subtraction
uint32_t FPU::executeAddSub(uint32_t a, uint32_t b, bool is_sub) const
{
    FloatData A(a), B(b);
    if (is_sub)
        B.sign ^= 1U; // Invert sign for subtraction

    // Edge case checks: NaN, Infinity, and Zeros
    if (A.isNaN || B.isNaN)
        return IEEE754_CANONICAL_NAN;
    if (A.isInf && B.isInf)
        return (A.sign == B.sign) ? ((A.sign << 31) | IEEE754_INFINITY_MASK) : IEEE754_CANONICAL_NAN;
    if (A.isInf)
        return (A.sign << 31) | IEEE754_INFINITY_MASK;
    if (B.isInf)
        return (B.sign << 31) | IEEE754_INFINITY_MASK;
    if (A.isZero && B.isZero)
        return (A.sign == B.sign) ? (A.sign << 31) : 0;
    if (A.isZero)
        return (B.sign << 31) | (static_cast<uint32_t>(B.exp) << 23) | (B.mant & 0x7FFFFFU);
    if (B.isZero)
        return (A.sign << 31) | (static_cast<uint32_t>(A.exp) << 23) | (A.mant & 0x7FFFFFU);

    // Shift left by 3 bits to make room for Guard, Round, and Sticky bits
    uint32_t mantA = barrelShiftLeft(A.mant, 3);
    uint32_t mantB = barrelShiftLeft(B.mant, 3);
    int32_t expA = A.exp, expB = B.exp;
    uint32_t signA = A.sign, signB = B.sign;

    // Ensure operand A has the larger magnitude
    int32_t expDiffSwap = static_cast<int32_t>(koggeStoneAdder32(expA, ~expB, true).sum);
    int32_t mantDiffSwap = static_cast<int32_t>(koggeStoneAdder32(mantA, ~mantB, true).sum);

    if ((expDiffSwap < 0) || (expDiffSwap == 0 && mantDiffSwap < 0))
    {
        std::swap(expA, expB);
        std::swap(mantA, mantB);
        std::swap(signA, signB);
    }

    // Align mantissas to the larger exponent
    int32_t expDiff = static_cast<int32_t>(koggeStoneAdder32(expA, ~expB, true).sum);
    if (expDiff > 0)
    {
        uint32_t sticky = 0;
        if (expDiff >= 27)
        {
            sticky = (mantB != 0);
            mantB = 0;
        }
        else
        {
            uint32_t mask = koggeStoneAdder32(barrelShiftLeft(1U, expDiff), ~1U, true).sum;
            sticky = (mantB & mask) != 0;
            mantB = barrelShiftRight(mantB, expDiff);
        }
        mantB |= sticky; // Preserve discarded bits in sticky flag
    }

    uint32_t resMant = 0;
    int32_t resExp = expA;
    uint32_t resSign = signA;

    if (signA == signB)
    {
        // Addition: add mantissas
        resMant = koggeStoneAdder32(mantA, mantB, false).sum;
        if (resMant & (1U << 27))
        { // Overflow carry out: shift right by 1
            resMant = (resMant >> 1) | (resMant & 1U);
            resExp = static_cast<int32_t>(koggeStoneAdder32(resExp, 1U, false).sum);
        }
    }
    else
    {
        // Subtraction: subtract smaller mantissa from larger
        resMant = koggeStoneAdder32(mantA, ~mantB, true).sum;
        if (resMant == 0)
            return 0; // Identical values cancel to 0

        // Normalization: shift leading 1 back to bit 26 using leading zero count
        uint32_t lzc = countLeadingZeros32(resMant);
        if (lzc > 5 && resExp > 0)
        {
            uint32_t shift = koggeStoneAdder32(lzc, ~5U, true).sum;
            if (shift > static_cast<uint32_t>(resExp))
            {
                shift = static_cast<uint32_t>(resExp);
            }
            resMant = barrelShiftLeft(resMant, shift);
            resExp = static_cast<int32_t>(koggeStoneAdder32(resExp, ~shift, true).sum);
        }
    }

    // Extract GRS bits and round result
    bool G = (resMant >> 2) & 1U;
    bool R = (resMant >> 1) & 1U;
    bool S = resMant & 1U;
    resMant = barrelShiftRight(resMant, 3);

    return roundAndPack(resSign, resExp, resMant, G, R, S);
}
