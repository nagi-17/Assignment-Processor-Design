#ifndef FPU_HPP
#define FPU_HPP

#include <cstdint>

// Stores the final 32-bit float result and architectural status flags
struct FPUResult
{
    uint32_t res; // Computed IEEE-754 32-bit float
    bool Z;       // Zero flag: set if result is +/- 0
    bool N;       // Negative flag: set if sign bit is 1
    bool V;       // Overflow flag: set if result exceeds range (Infinity)
    bool NaN;     // Invalid flag: set if result is Not-a-Number
};

// Holds 32-bit sum and carry-out to allow chaining adders together
struct AdderResult32
{
    uint32_t sum;
    bool cout;
};

class FPU
{
public:
    enum FPUOp : uint8_t
    {
        OP_FADD,
        OP_FSUB,
        OP_FMUL,
        OP_FDIV,
        OP_FCMP
    };

    // Main entry point: decodes operation, executes, and sets status flags   (fpu_helpers.cpp)
    FPUResult execute(uint32_t operand1, uint32_t operand2, FPUOp control_signal);

private:
    static constexpr uint32_t IEEE754_CANONICAL_NAN = 0x7FC00000U;
    static constexpr uint32_t IEEE754_INFINITY_MASK = 0x7F800000U;
    static constexpr uint32_t TWO_Q30 = 0x80000000U; // Fixed-point 2.0 in Q2.30

    // Unpacks 32-bit float into sign, biased exponent, and 24-bit hidden-bit mantissa
    // (constructor defined in fpu_helpers.cpp)
    struct FloatData
    {
        uint32_t sign;
        int32_t exp;
        uint32_t mant;
        bool isZero, isInf, isNaN;

        FloatData(uint32_t val);
    };

    // ---- fpu_helpers.cpp: gate-level building blocks ----
    uint32_t barrelShiftLeft(uint32_t data, uint32_t shift_amount) const;
    uint32_t barrelShiftRight(uint32_t data, uint32_t shift_amount) const;
    AdderResult32 koggeStoneAdder32(uint32_t a, uint32_t b, bool carry_in = false) const;
    void carrySaveAdder(uint32_t a, uint32_t b, uint32_t c,
                        uint32_t &sum, uint32_t &carry) const;
    uint32_t countLeadingZeros32(uint32_t x) const;

    // ---- fpu_helpers.cpp: rounding / packing ----
    uint32_t roundAndPack(uint32_t sign, int32_t exp, uint32_t mant,
                          bool G, bool R, bool S) const;

    // ---- fpu_addsub.cpp ----
    uint32_t executeAddSub(uint32_t a, uint32_t b, bool is_sub) const;

    // ---- fpu_muldiv.cpp: multiplier ----
    uint32_t boothSelect(uint32_t M, uint32_t neg_M, uint8_t window, uint32_t shift) const;
    uint32_t boothWallaceKoggeMul16(uint32_t M, uint32_t Q) const;
    void fastMul32(uint32_t a, uint32_t b, uint32_t &prod_hi, uint32_t &prod_lo) const;
    uint32_t mulQ30(uint32_t a, uint32_t b) const;
    uint32_t executeMul(uint32_t a, uint32_t b) const;

    // ---- fpu_muldiv.cpp: divider ----
    uint32_t newtonRaphsonStep(uint32_t D, uint32_t X) const;
    uint32_t newtonRaphsonReciprocal24(uint32_t divisor_mant) const;
    uint32_t estimateQuotient(uint32_t a, uint32_t X) const;
    void quotientStepDown(uint32_t &q, uint32_t &r, uint32_t b) const;
    void quotientStepUp(uint32_t &q, uint32_t &r, uint32_t b) const;
    void refineQuotient(uint32_t a, uint32_t b, uint32_t X,
                        uint32_t &q, bool &inexact) const;
    void divideMantissas(uint32_t a, uint32_t b, uint32_t &q, bool &inexact) const;
    uint32_t executeDiv(uint32_t a, uint32_t b) const;

    // ---- fpu_cmp.cpp ----
    void executeCmp(uint32_t a, uint32_t b, FPUResult &out) const;
};

#endif // FPU_HPP
