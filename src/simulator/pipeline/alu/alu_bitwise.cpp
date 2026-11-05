#include "alu.hpp"

ALUResult ALU::bitwiseLogic(uint32_t a, uint32_t b, ALUOp op)
{
    ALUResult temp = {0, false, false, false, false};

    if (op == OP_AND)
        temp.res = a & b;
    else if (op == OP_OR)
        temp.res = a | b;
    else if (op == OP_XOR)
        temp.res = a ^ b;
    else if (op == OP_NOT)
        temp.res = ~a;

    temp.C = false;
    temp.V = false;

    return temp;
}

ALUResult ALU::barrelShifter(uint32_t data, uint32_t shift_amount, ALUOp op)
{
    ALUResult temp = {0, false, false, false, false};

    uint32_t relbit = shift_amount & 0x1F;
    uint32_t result = data;

    bool msb = (data >> 31) & 1;

    if (op == OP_LSL)
    {
        if (relbit & 0b00001)
            result = result << 1;
        if (relbit & 0b00010)
            result = result << 2;
        if (relbit & 0b00100)
            result = result << 4;
        if (relbit & 0b01000)
            result = result << 8;
        if (relbit & 0b10000)
            result = result << 16;

        if (relbit > 0)
            temp.C = (data >> (32 - relbit)) & 1;
    }
    else
    {
        if (relbit & 0b00001)
        {
            result = result >> 1;
            if (op == OP_ASR && msb)
                result |= 0b10000000000000000000000000000000;
        }

        if (relbit & 0b00010)
        {
            result = result >> 2;
            if (op == OP_ASR && msb)
                result |= 0b11000000000000000000000000000000;
        }

        if (relbit & 0b00100)
        {
            result = result >> 4;
            if (op == OP_ASR && msb)
                result |= 0b11110000000000000000000000000000;
        }

        if (relbit & 0b01000)
        {
            result = result >> 8;
            if (op == OP_ASR && msb)
                result |= 0b11111111000000000000000000000000;
        }

        if (relbit & 0b10000)
        {
            result = result >> 16;
            if (op == OP_ASR && msb)
                result |= 0b11111111111111110000000000000000;
        }

        if (relbit > 0)
            temp.C = (data >> (relbit - 1)) & 1;
    }

    temp.res = result;
    temp.V = false;

    return temp;
}
