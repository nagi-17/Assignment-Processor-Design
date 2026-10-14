#ifndef FPU_HPP
#define FPU_HPP

#include <bits/stdc++.h>
using namespace std;

struct FPUResult {
    uint32_t res; // Result of operation
    bool Z;       // Zero Flag (for comapre & 0)
    bool N;       // Negative Flag (for compare)
    bool V;       // Overflow Flag
    bool NaN;     // Not-a-Number Flag
};

struct GPBlock {
    bool g; // Generate
    bool p; // Propagate
};

class FPU {
public:
    enum FPUOp : uint8_t
    {
        OP_FADD,
        OP_FSUB,
        OP_FMUL,
        OP_FDIV,
        OP_FCMP
    };

    FPUResult execute(uint32_t operand1, uint32_t operand2, FPUOp control_signal);

private:
    const uint32_t NAN_VAL = 0x7FC00000;
    const uint32_t INF_VAL = 0x7F800000;

    // Representation of floating number
    struct FloatData {
        uint32_t sign;
        int32_t exp;
        uint32_t mant;
        bool isZero, isInf, isNaN;

        FloatData(uint32_t val) {
            sign = (val >> 31) & 1; // the 31st bit is sign bit
            exp = (val >> 23) & 0xFF; // gets the bits 30-23 (8 bits) 
            mant = val & 0x7FFFFF; // mantissa is last 23 bits
            
            if (exp == 0) mant = 0; // Flush subnormals(very close to 0) to 0
            // for simplicity we are not considering denormal numbers

            isZero = (exp == 0 && mant == 0);
            isInf = (exp == 255 && mant == 0);
            isNaN = (exp == 255 && mant != 0);
            
            // Reinsert implicit leading 1 (as mantissa store value after 1. )
            if (!isZero && !isInf && !isNaN) mant |= 0x800000; 
        }
    };

    // Helper method prototypes
    uint32_t barrelShiftLeft(uint32_t data, uint32_t shift_amount);
    uint32_t barrelShiftRight(uint32_t data, uint32_t shift_amount);
    uint32_t hardware_adder(uint32_t A, uint32_t B, bool Cin = 0);
    uint32_t roundAndPack(uint32_t sign, int32_t exp, uint32_t mant, bool G, bool R, bool S);

    // Core operation prototypes
    uint32_t executeAddSub(uint32_t a, uint32_t b, bool is_sub);
    uint32_t executeMul(uint32_t a, uint32_t b);
    uint32_t executeDiv(uint32_t a, uint32_t b);
    void executeCmp(uint32_t a, uint32_t b, FPUResult& out);
};

#endif