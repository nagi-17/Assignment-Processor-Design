#include "alu.hpp"

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

// Leading Zero Counter: binary search tree using bitwise OR (returns 32 for x == 0)
uint32_t ALU::countLeadingZeros(uint32_t x)
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

// Unsigned radix-4 divider with early termination.
// Latency: ceil(bit_length(quotient)/2) iterations, worst case 16, 0 when dividend < divisor.
// Divide by zero returns res = 0 with V = 1 (same behaviour as before).
ALUResult ALU::radix4Divider(uint32_t dividend, uint32_t divisor, ALUOp op)
{
    ALUResult temp = {0, false, false, false, false};
    div_iterations = 0;

    if (divisor == 0)
    {
        temp.V = true;
        return temp;
    }

    uint32_t quotient = 0;
    uint32_t remainder = dividend;

    // dividend >= divisor  <=>  carry out of dividend + ~divisor + 1
    bool ge = koggeStoneAdder(dividend, ~divisor, 1).C;

    if (ge)
    {
        // ---- Alignment: find the bit length of the quotient ----
        // t = nd - nm = clz(divisor) - clz(dividend)   (0..31)
        uint32_t clzM = countLeadingZeros(divisor);
        uint32_t clzD = countLeadingZeros(dividend);
        uint32_t t = koggeStoneAdder(clzM, ~clzD, 1).res;

        // top = dividend aligned to the divisor's bit length; quotient has t or t+1 bits
        uint32_t top = dividend >> t;
        bool topGeM = koggeStoneAdder(top, ~divisor, 1).C;
        uint32_t qbits = koggeStoneAdder(t, 0, topGeM ? 1 : 0).res; // bit length of quotient

        // s = qbits rounded up to an even number (radix 4 consumes 2 bits per iteration)
        uint32_t s = koggeStoneAdder(qbits, 1, 0).res & ~1U;
        uint32_t iterations = s >> 1;
        div_iterations = iterations;

        // ---- Multiples of the divisor (33/34-bit values) ----
        uint64_t M1 = divisor;
        uint64_t M2 = M1 << 1;
        uint64_t M3 = koggeStoneAdder64(M1, M2, 0).sum;

        // ---- Digit recurrence ----
        uint64_t R = static_cast<uint64_t>(dividend) >> s; // < divisor, s <= 32
        uint32_t Q = 0;

        for (uint32_t i = iterations; i-- > 0;)
        {
            uint64_t bits = (dividend >> (2 * i)) & 0x3;
            uint64_t R4 = (R << 2) | bits;

            // Three parallel subtractors R4 - kM; carry out = 1  <=>  R4 >= kM
            Adder64Result d1 = koggeStoneAdder64(R4, ~M1, 1);
            Adder64Result d2 = koggeStoneAdder64(R4, ~M2, 1);
            Adder64Result d3 = koggeStoneAdder64(R4, ~M3, 1);

            uint32_t digit;
            if (d3.cout)
            {
                digit = 3;
                R = d3.sum;
            }
            else if (d2.cout)
            {
                digit = 2;
                R = d2.sum;
            }
            else if (d1.cout)
            {
                digit = 1;
                R = d1.sum;
            }
            else
            {
                digit = 0;
                R = R4;
            }

            Q = (Q << 2) | digit;
        }

        quotient = Q;
        remainder = static_cast<uint32_t>(R);
    }
    else
    {
        quotient = 0; // dividend < divisor: remainder is the dividend itself
    }

    if (op == OP_DIV)
        temp.res = quotient;
    else
        temp.res = remainder;

    temp.Z = (temp.res == 0);
    temp.N = (temp.res >> 31) & 1;
    temp.C = false;

    return temp;
}
