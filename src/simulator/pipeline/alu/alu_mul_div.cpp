#include <cstdint>
#include <vector>
#include <iostream>

struct ALUResult
{
    uint32_t res;
    bool Z;
    bool N;
    bool C;
    bool V;
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

private:
    ALUResult koggeStoneAdder(uint32_t a, uint32_t b, int carry_in);
    ALUResult bitwiseLogic(uint32_t a, uint32_t b, ALUOp op);
    ALUResult barrelShifter(uint32_t data, uint32_t shift_amount, ALUOp op);
    void carrySaveAdder(uint32_t a, uint32_t b, uint32_t c,
                        uint32_t &sum, uint32_t &carry);
    ALUResult boothWallaceKoggeMultiplier(uint32_t multiplicand, uint32_t multiplier);
    ALUResult nonRestoringDivider(uint32_t dividend, uint32_t divisor, ALUOp op);
};
void ALU::carrySaveAdder(uint32_t a, uint32_t b, uint32_t c,
                             uint32_t &sum, uint32_t &carry)
{
    sum = a ^ b ^ c;
    carry = ((a & b) | (b & c) | (c & a)) << 1;
}

ALUResult ALU::boothWallaceKoggeMultiplier(uint32_t multiplicand,
                                           uint32_t multiplier)
{
    ALUResult temp = {0, false, false, false, false};

    uint32_t M = multiplicand;
    uint32_t Q = multiplier;

    std::vector<uint32_t> rows;
    uint32_t Q_minus_1 = 0;

    // Radix-4 Booth generation
    for (int i = 0; i < 16; i++)
    {
        uint8_t window = ((Q & 0b11) << 1) | Q_minus_1;

        uint32_t partial_product = 0;
        uint32_t carry_in = 0;

        switch (window)
        {
        case 0b000:
        case 0b111:
            partial_product = 0;
            break;

        case 0b001:
        case 0b010:
            partial_product = M;
            break;

        case 0b011:
            partial_product = M << 1;
            break;

        case 0b100:
            partial_product = ~(M << 1);
            carry_in = 1;
            break;

        case 0b101:
        case 0b110:
            partial_product = ~M;
            carry_in = 1;
            break;
        }

        uint32_t shifted_pp = partial_product << (i * 2);

        if (carry_in == 1)
            shifted_pp = koggeStoneAdder(
                shifted_pp, 1 << (i * 2), 0
            ).res;

        rows.push_back(shifted_pp);

        Q_minus_1 = (Q >> 1) & 1;
        Q = Q >> 2;
    }

    // Wallace tree compression
    while (rows.size() > 2)
    {
        std::vector<uint32_t> next_layer_rows;

        size_t i = 0;
        for (; i + 2 < rows.size(); i += 3)
        {
            uint32_t sum, carry;
            carrySaveAdder(rows[i], rows[i + 1], rows[i + 2], sum, carry);

            next_layer_rows.push_back(sum);
            next_layer_rows.push_back(carry);
        }

        while (i < rows.size())
        {
            next_layer_rows.push_back(rows[i]);
            i++;
        }

        rows = next_layer_rows;
    }

    if (rows.size() == 2)
        temp = koggeStoneAdder(rows[0], rows[1], 0);
    else if (rows.size() == 1)
        temp.res = rows[0];

    temp.C = false;
    temp.V = false;

    return temp;
}

ALUResult ALU::nonRestoringDivider(uint32_t dividend,
                                   uint32_t divisor,
                                   ALUOp op)
{
    ALUResult temp = {0, false, false, false, false};

    if (divisor == 0)
    {
        temp.V = true;
        return temp;
    }

    int64_t A = 0;
    int64_t M = divisor;
    uint32_t Q = dividend;

    // 32 iterations
    for (int i = 0; i < 32; i++)
    {
        // Shift A left and bring in MSB of Q
        A = (A << 1) | ((Q >> 31) & 1);

        // Shift Q left
        Q <<= 1;

        // Non-restoring step
        if (A >= 0)
            A -= M;
        else
            A += M;

        // Determine next quotient bit
        if (A >= 0)
            Q |= 1;
    }

    // Final restoration
    if (A < 0)
        A += M;

    if (op == OP_DIV)
        temp.res = Q;
    else
        temp.res = static_cast<uint32_t>(A);

    temp.Z = (temp.res == 0);
    temp.N = (temp.res >> 31) & 1;
    temp.C = false;

    return temp;
}