#pragma once
#include <cstdint>
#include <vector>
#include <iostream>

// Flag conventions
//   Z : result == 0
//   N : result bit 31
//   C : ADD  -> carry out
//   V : ADD/SUB signed overflow; DIV/MOD -> divide by zero; others -> 0
struct ALUResult
{
    uint32_t res;
    bool Z;
    bool N;
    bool C;
    bool V;
};

struct Adder64Result
{
    uint64_t sum;
    bool cout;
};

class ALU
{
public:
    enum ALUOp : uint8_t
    {
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_MOD,
        OP_CMP,
        OP_LD,
        OP_ST,
        OP_MOV,
        OP_MOVU,
        OP_MOVH,
        OP_AND,
        OP_OR,
        OP_NOT,
        OP_XOR,
        OP_LSL,
        OP_ASR,
        OP_LSR,
        OP_CHK
    };

    ALUResult execute(uint32_t operand1, uint32_t operand2, ALUOp control_signal);

    uint32_t lastDivIterations() const { return div_iterations; }

private:
    ALUResult koggeStoneAdder(uint32_t a, uint32_t b, int carry_in);
    Adder64Result koggeStoneAdder64(uint64_t a, uint64_t b, int carry_in);
    ALUResult bitwiseLogic(uint32_t a, uint32_t b, ALUOp op);
    ALUResult barrelShifter(uint32_t data, uint32_t shift_amount, ALUOp op);
    void carrySaveAdder(uint32_t a, uint32_t b, uint32_t c,
                        uint32_t &sum, uint32_t &carry);
    ALUResult boothWallaceKoggeMultiplier(uint32_t multiplicand, uint32_t multiplier);
    uint32_t countLeadingZeros(uint32_t x);
    ALUResult radix4Divider(uint32_t dividend, uint32_t divisor, ALUOp op);

    uint32_t div_iterations = 0;
};
