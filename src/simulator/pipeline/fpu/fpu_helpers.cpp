#include "fpu.hpp"

// Main entry point: decodes operation, executes, and sets status flags
FPUResult FPU::execute(uint32_t operand1, uint32_t operand2, FPUOp control_signal)
{
    FPUResult out = {0, false, false, false, false};

    switch (control_signal)
    {
    case OP_FADD:
        out.res = executeAddSub(operand1, operand2, false);
        break;
    case OP_FSUB:
        out.res = executeAddSub(operand1, operand2, true);
        break;
    case OP_FMUL:
        out.res = executeMul(operand1, operand2);
        break;
    case OP_FDIV:
        out.res = executeDiv(operand1, operand2);
        break;
    case OP_FCMP:
        executeCmp(operand1, operand2, out);
        return out; // CMP updates flags directly and returns early
    default:
        break;
    }

    // Unpack exponent and mantissa to determine output flags
    uint32_t exp = (out.res >> 23) & 0xFFU;
    uint32_t mant = out.res & 0x7FFFFFU;

    out.Z = (exp == 0 && mant == 0);
    out.N = (out.res >> 31) & 1U;
    out.V = (exp == 255 && mant == 0);
    out.NaN = (exp == 255 && mant != 0);

    return out;
}

// ===== Bitwise / structural primitives =====

// Barrel shifter: shifts left in 5 parallel stages (1, 2, 4, 8, 16 bits)
uint32_t FPU::barrelShiftLeft(uint32_t data, uint32_t shift_amount) const
{
    if (shift_amount >= 32)
        return 0;
    uint32_t result = data;
    if (shift_amount & 0b00001U)
        result <<= 1;
    if (shift_amount & 0b00010U)
        result <<= 2;
    if (shift_amount & 0b00100U)
        result <<= 4;
    if (shift_amount & 0b01000U)
        result <<= 8;
    if (shift_amount & 0b10000U)
        result <<= 16;
    return result;
}

// Barrel shifter: shifts right in 5 parallel stages (1, 2, 4, 8, 16 bits)
uint32_t FPU::barrelShiftRight(uint32_t data, uint32_t shift_amount) const
{
    if (shift_amount >= 32)
        return 0;
    uint32_t result = data;
    if (shift_amount & 0b00001U)
        result >>= 1;
    if (shift_amount & 0b00010U)
        result >>= 2;
    if (shift_amount & 0b00100U)
        result >>= 4;
    if (shift_amount & 0b01000U)
        result >>= 8;
    if (shift_amount & 0b10000U)
        result >>= 16;
    return result;
}

// Kogge-Stone Adder: parallel-prefix adder calculating all carries in 5 stages
AdderResult32 FPU::koggeStoneAdder32(uint32_t a, uint32_t b, bool carry_in) const
{
    uint32_t G = a & b; // Generate bit
    uint32_t P = a ^ b; // Propagate bit
    uint32_t cin_mask = carry_in ? 1U : 0U;
    G |= (P & cin_mask);

    // 5-stage carry generation tree (O(log N) depth, zero loops)
    G |= (P & (G << 1));
    P &= (P << 1);
    G |= (P & (G << 2));
    P &= (P << 2);
    G |= (P & (G << 4));
    P &= (P << 4);
    G |= (P & (G << 8));
    P &= (P << 8);
    G |= (P & (G << 16));

    uint32_t carries = (G << 1) | cin_mask;
    uint32_t sum = (a ^ b) ^ carries;
    bool cout = (G >> 31) & 1U;

    return {sum, cout};
}

// 3:2 Carry-Save Adder: compresses 3 inputs into sum and carry vectors
void FPU::carrySaveAdder(uint32_t a, uint32_t b, uint32_t c,
                    uint32_t &sum, uint32_t &carry) const
{
    sum = a ^ b ^ c;
    carry = ((a & b) | (b & c) | (c & a)) << 1;
}

// Leading Zero Counter: binary search tree using bitwise OR (no '+' used)
uint32_t FPU::countLeadingZeros32(uint32_t x) const
{
    if (x == 0)
        return 32;
    uint32_t n = 0;
    if ((x & 0xFFFF0000U) == 0)
    {
        n |= 16U;
        x <<= 16;
    }
    if ((x & 0xFF000000U) == 0)
    {
        n |= 8U;
        x <<= 8;
    }
    if ((x & 0xF0000000U) == 0)
    {
        n |= 4U;
        x <<= 4;
    }
    if ((x & 0xC0000000U) == 0)
    {
        n |= 2U;
        x <<= 2;
    }
    if ((x & 0x80000000U) == 0)
    {
        n |= 1U;
    }
    return n;
}

// ===== Unpack / round / pack =====

// Unpacks 32-bit float into sign, biased exponent, and 24-bit hidden-bit mantissa
FPU::FloatData::FloatData(uint32_t val)
{
    sign = (val >> 31) & 1U;
    exp = static_cast<int32_t>((val >> 23) & 0xFFU);
    mant = val & 0x7FFFFFU;

    if (exp == 0)
        mant = 0; // Flush denormal numbers to 0

    isZero = (exp == 0 && mant == 0);
    isInf = (exp == 255 && mant == 0);
    isNaN = (exp == 255 && mant != 0);

    // Restore the implicit leading 1 for normalized numbers (1.mantissa)
    if (!isZero && !isInf && !isNaN)
        mant |= 0x800000U;
}

// Rounds result using IEEE-754 round-to-nearest-even and packs back into 32 bits
uint32_t FPU::roundAndPack(uint32_t sign, int32_t exp, uint32_t mant,
                      bool G, bool R, bool S) const
{
    // Nearest-even rounding condition using Guard (G), Round (R), and Sticky (S) bits
    if (G && (R || S || (mant & 1U)))
    {
        mant = koggeStoneAdder32(mant, 1U, false).sum;
        if (mant & (1U << 24))
        { // Overflow past 24 bits: shift right and bump exponent
            mant >>= 1;
            exp = static_cast<int32_t>(koggeStoneAdder32(exp, 1U, false).sum);
        }
    }

    mant &= 0x7FFFFFU; // Strip the implicit leading 1 bit

    // Handle overflow and underflow boundaries
    if (exp >= 255)
        return (sign << 31) | IEEE754_INFINITY_MASK;
    if (exp <= 0)
        return (sign << 31);

    return (sign << 31) | (static_cast<uint32_t>(exp) << 23) | mant;
}
