// FPU floating-point comparison
#include "fpu.hpp"

// Floating-point Comparison
void FPU::executeCmp(uint32_t a, uint32_t b, FPUResult &out) const
{
    FloatData A(a), B(b);
    out.res = 0;

    if (A.isNaN || B.isNaN)
    {
        out.NaN = true;
        return;
    }
    if (A.isZero && B.isZero)
    {
        out.Z = true;
        return;
    }
    if (A.sign != B.sign)
    {
        out.N = (A.sign == 1U); // Positive > Negative
        return;
    }

    bool isNeg = (A.sign == 1U);
    int32_t expDiff = static_cast<int32_t>(koggeStoneAdder32(A.exp, ~B.exp, true).sum);
    int32_t mantDiff = static_cast<int32_t>(koggeStoneAdder32(A.mant, ~B.mant, true).sum);

    // Compare exponents first, then mantissas if exponents are equal
    if (expDiff != 0)
    {
        out.N = (expDiff < 0) ^ isNeg;
    }
    else if (mantDiff != 0)
    {
        out.N = (mantDiff < 0) ^ isNeg;
    }
    else
    {
        out.Z = true; // Both operands are equal
    }
}
