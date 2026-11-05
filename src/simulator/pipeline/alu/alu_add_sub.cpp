#include "alu.hpp"

ALUResult ALU::execute(uint32_t operand1, uint32_t operand2,
                         ALUOp control_signal)
{
    ALUResult out = {0, false, false, false, false};

    switch (control_signal)
    {
    case OP_ADD:
    case OP_LD:
    case OP_ST:
        out = koggeStoneAdder(operand1, operand2, 0);
        break;

    case OP_SUB:
    case OP_CMP:
    case OP_CHK:
        out = koggeStoneAdder(operand1, ~operand2, 1);
        break;

    case OP_AND:
    case OP_OR:
    case OP_XOR:
    case OP_NOT:
        out = bitwiseLogic(operand1, operand2, control_signal);
        break;

    case OP_LSL:
    case OP_ASR:
    case OP_LSR:
        out = barrelShifter(operand1, operand2, control_signal);
        break;

    case OP_MOV:
        out.res = operand2;
        out.C = false;
        out.V = false;
        break;

    case OP_MOVU:
        out.res = operand2 & 0x0000FFFF;
        out.C = false;
        out.V = false;
        break;

    case OP_MOVH:
        out.res = operand2 << 16;
        out.C = false;
        out.V = false;
        break;

    case OP_MUL:
        out = boothWallaceKoggeMultiplier(operand1, operand2);
        break;

    case OP_DIV:
    case OP_MOD:
        out = radix4Divider(operand1, operand2, control_signal);
        break;

    default:
        break;
    }

    out.Z = (out.res == 0);
    out.N = (out.res >> 31) & 1;

    return out;
}

ALUResult ALU::koggeStoneAdder(uint32_t a, uint32_t b, int carry_in)
{
    ALUResult temp = {0, false, false, false, false};

    uint32_t G = a & b;
    uint32_t P = a ^ b;

    uint32_t cin_mask = carry_in ? 1 : 0;
    G |= (P & cin_mask);

    // Kogge-Stone prefix network
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
    temp.res = (a ^ b) ^ carries;

    temp.C = (G >> 31) & 1;

    uint32_t sign_a = (a >> 31) & 1;
    uint32_t sign_b = (b >> 31) & 1;
    uint32_t sign_sum = (temp.res >> 31) & 1;

    temp.V = (sign_a == sign_b) && (sign_a != sign_sum);

    return temp;
}

// 64-bit Kogge-Stone adder (6 prefix stages). The divider datapath needs 34-bit
// arithmetic (4 * remainder + 2 new dividend bits), which does not fit in 32 bits.
Adder64Result ALU::koggeStoneAdder64(uint64_t a, uint64_t b, int carry_in)
{
    uint64_t G = a & b;
    uint64_t P = a ^ b;

    uint64_t cin_mask = carry_in ? 1 : 0;
    G |= (P & cin_mask);

    G |= (P & (G << 1));
    P &= (P << 1);

    G |= (P & (G << 2));
    P &= (P << 2);

    G |= (P & (G << 4));
    P &= (P << 4);

    G |= (P & (G << 8));
    P &= (P << 8);

    G |= (P & (G << 16));
    P &= (P << 16);

    G |= (P & (G << 32));

    uint64_t carries = (G << 1) | cin_mask;

    Adder64Result r;
    r.sum = (a ^ b) ^ carries;
    r.cout = (G >> 63) & 1;
    return r;
}
