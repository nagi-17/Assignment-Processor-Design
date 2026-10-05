#include <iostream>
#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <stack>
#include <deque>
#include <string>
#include <cmath>
#include <climits>
#include <cstring>
#include <numeric>
#include <tuple>

struct ALUResult
{
    uint32_t res; // Result of operation
    bool Z;       // Zero Flag
    bool N;       // Negative Flag
    bool C;       // Carry Flag
    bool V;       // Overflow Flag
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

    ALUResult execute(uint32_t operand1, uint32_t operand2, ALUOp control_signal)
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
            // the control unit essentially bypasses the alu in case of mov instruction
            // the rs1 field will be empty for mov instruction and the data, either the immediate or the register data will definitely
            // be routed through operand 2.
            out.res = operand2;
            out.C = false;
            out.V = false;
            break;
        case OP_MOVU:
            // Hardware mask: AND with 0x0000FFFF to strip the sign extension
            // saves mux and complexity in decode stage
            out.res = operand2 & 0x0000FFFF;
            out.C = false;
            out.V = false;
            break;
        case OP_MOVH:
            // Hardware bypass: Route input directly to output by connecting the bottom 16 to top 16 of output and connecting rest t0 ground
            out.res = operand2 << 16;
            out.C = false;
            out.V = false;
            break;
        case OP_MUL:
            out = boothWallaceKoggeMultiplier(operand1, operand2);
            break;
        case OP_DIV: //DIV WILL TAKE 32 clock cycles. rest will take 1 clock cycle, thus it is the speed bottleneck of our system 
        case OP_MOD:
            out = nonRestoringDivider(operand1, operand2, control_signal);
            break;

        default:
            break;
        }

        // Universal Flags
        out.Z = (out.res == 0);
        out.N = (out.res >> 31) & 1;

        return out;
    }

private:
    ALUResult koggeStoneAdder(uint32_t a, uint32_t b, int carry_in)
    {
        ALUResult temp = {0, false, false, false, false};

        // 1. Pre-processing: Compute initial Generate (G) and Propagate (P)
        uint32_t G = a & b;
        uint32_t P = a ^ b;

        // Inject carry_in into the 0th bit's Generate signal
        uint32_t cin_mask = carry_in ? 1 : 0;
        G |= (P & cin_mask);

        // 2. The Prefix Network: 5 Overlapping Parallel Stages
        // Each bitwise operation simulates an entire layer of physical logic gates

        // Stage 1 (Shift by 1)
        // divides into block of 2 with the most significant bit of the block containing the information about the whol block
        G |= (P & (G << 1));
        P &= (P << 1);

        // Stage 2 (Shift by 2)
        // divides into blocks of sizes <=4
        G |= (P & (G << 2));
        P &= (P << 2);

        // Stage 3 (Shift by 4)
        // sizes<=8
        G |= (P & (G << 4));
        P &= (P << 4);

        // Stage 4 (Shift by 8)
        // sizes <=16
        G |= (P & (G << 8));
        P &= (P << 8);

        // Stage 5 (Shift by 16)
        // sizes<=32
        G |= (P & (G << 16));

        // 'G' now holds the Carry-Out for every single bit position.
        // To compute the Sum, bit 'i' needs the Carry-Out from bit 'i-1'.
        // We shift G left by 1 to align the carries, and drop the initial Cin into bit 0.

        uint32_t carries = (G << 1) | cin_mask;
        temp.res = (a ^ b) ^ carries; // Sum = p_i XOR c_{i-1}

        temp.C = (G >> 31) & 1; // The Carry Flag is the Carry-Out of the highest bit (31)

        uint32_t sign_a = (a >> 31) & 1;
        uint32_t sign_b = (b >> 31) & 1;
        uint32_t sign_sum = (temp.res >> 31) & 1;
        temp.V = (sign_a == sign_b) && (sign_a != sign_sum);

        return temp;
    }
    ALUResult bitwiseLogic(uint32_t a, uint32_t b, ALUOp op)
    {
        ALUResult temp = {0, false, false, false, false};

        if (op == OP_AND)
            temp.res = a & b;
        else if (op == OP_OR)
            temp.res = a | b;
        else if (op == OP_XOR)
            temp.res = a ^ b;
        else if (op == OP_NOT)
            temp.res = ~a; // NOT is a unary operator

        temp.C = false;
        temp.V = false;

        return temp;
    }

    ALUResult barrelShifter(uint32_t data, uint32_t shift_amount, ALUOp op)
    {
        ALUResult temp = {0, false, false, false, false};

        // barrel shifter cuts down hardwiring and silicon costs by 85% that would have otherwise been incurred if we went for
        // complete bruteforce approach to shifting the bits

        // Hardware mask: only the bottom 5 bits are wired to the MUXes (0-31 max)
        uint32_t relbit = shift_amount & 0x1F; // only 5 bits are relevant
        uint32_t result = data;

        // Extract the MSB (Sign Bit) for Arithmetic Shift Right routing
        bool msb = (data >> 31) & 1;

        if (op == OP_LSL)
        {
            // use binary logic in order to lopgarithm
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

            // Carry Flag: The last bit shifted out the left side
            if (relbit > 0)
            {
                temp.C = (data >> (32 - relbit)) & 1;
            }
        }
        else
        {
            // Right Shift Multiplexer Layers (Shared by LSR and ASR)
            // If the operation is ASR and the number is negative, we explicitly OR the specific bitmasks to mimic the hardware sign-extension wires.
            if (relbit & 0b00001)
            {
                result = result >> 1;
                if (op == OP_ASR && msb)
                    result |= 0b1000'0000'0000'0000'0000'0000'0000'0000;
            }
            if (relbit & 0b00010)
            {
                result = result >> 2;
                if (op == OP_ASR && msb)
                    result |= 0b1100'0000'0000'0000'0000'0000'0000'0000;
            }
            if (relbit & 0b00100)
            {
                result = result >> 4;
                if (op == OP_ASR && msb)
                    result |= 0b1111'0000'0000'0000'0000'0000'0000'0000;
            }
            if (relbit & 0b01000)
            {
                result = result >> 8;
                if (op == OP_ASR && msb)
                    result |= 0b1111'1111'0000'0000'0000'0000'0000'0000;
            }
            if (relbit & 0b10000)
            {
                result = result >> 16;
                if (op == OP_ASR && msb)
                    result |= 0b1111'1111'1111'1111'0000'0000'0000'0000;
            }

            // Carry Flag: The last bit shifted out the right side
            if (relbit > 0)
            {
                temp.C = (data >> (relbit - 1)) & 1;
            }
        }

        temp.res = result;
        temp.V = false;

        return temp;
    }
    void carrySaveAdder(uint32_t a, uint32_t b, uint32_t c, uint32_t &sum, uint32_t &carry)
    {
        // Sum = A XOR B XOR C
        sum = a ^ b ^ c;
        // Carry = Majority Gate ((A AND B) OR (B AND C) OR (C AND A)), shifted left by 1
        carry = ((a & b) | (b & c) | (c & a)) << 1;
    }

    ALUResult boothWallaceKoggeMultiplier(uint32_t multiplicand, uint32_t multiplier)
    {
        ALUResult temp = {0, false, false, false, false};

        uint32_t M = multiplicand;
        uint32_t Q = multiplier;

        // 1. PHASE 1: Radix-4 Booth Combinational Generation
        // Generate all 16 partial products simultaneously
        std::vector<uint32_t> rows;
        uint32_t Q_minus_1 = 0;

        for (int i = 0; i < 16; i++)
        {
            // Extract the 3-bit window: Q[2i+1], Q[2i], Q[2i-1]
            uint8_t window = ((Q & 0b11) << 1) | Q_minus_1;

            uint32_t partial_product = 0;
            uint32_t carry_in = 0; // Needed for -M and -2M (Two's complement +1)

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
                carry_in = 1; // Complete the two's complement later
                break;
            case 0b101:
            case 0b110:
                partial_product = ~M;
                carry_in = 1;
                break;
            }

            // Shift the partial product into its proper binary position
            // In hardware, this is achieved by physical wire routing (hardwired shift)
            uint32_t shifted_pp = partial_product << (i * 2);

            // If the Booth operation was negative, we must add the +1 for two's complement.
            // In hardware, this +1 is injected directly into the Wallace tree as a separate bit.
            if (carry_in == 1)
            {
                shifted_pp = koggeStoneAdder(shifted_pp, 1 << (i * 2), 0).res;
            }

            rows.push_back(shifted_pp);

            // Prepare Q and Q_{-1} for the next window
            Q_minus_1 = (Q >> 1) & 1;
            Q = Q >> 2;
        }

        // 2. PHASE 2: Wallace Tree Compression
        // Compress the rows using arrays of Carry-Save Adders (3 rows -> 2 rows)
        // This loop simulates the hardware layers of the tree.
        while (rows.size() > 2)
        {
            std::vector<uint32_t> next_layer_rows;

            // Process rows in groups of 3
            size_t i = 0;
            for (; i + 2 < rows.size(); i += 3)
            {
                uint32_t sum, carry;
                // Feed 3 rows into the CSA network
                carrySaveAdder(rows[i], rows[i + 1], rows[i + 2], sum, carry);
                // The CSA outputs 2 rows
                next_layer_rows.push_back(sum);
                next_layer_rows.push_back(carry);
            }

            // If 1 or 2 rows are left over, just pass them directly to the next layer
            while (i < rows.size())
            {
                next_layer_rows.push_back(rows[i]);
                i++;
            }

            // Move to the next physical layer of the Wallace Tree
            rows = next_layer_rows;
        }

        // 3. PHASE 3: Kogge-Stone Addition
        // The Wallace tree always reduces down to exactly 2 rows.
        // Route them into your existing Carry-Propagate Adder.
        if (rows.size() == 2)
        {
            temp = koggeStoneAdder(rows[0], rows[1], 0);
        }
        else if (rows.size() == 1)
        {
            temp.res = rows[0]; // Edge case fallback
        }

        temp.C = false;
        temp.V = false; // V/C flags for multiplication require upper 32-bit analysis

        return temp;
    }

    ALUResult nonRestoringDivider(uint32_t dividend, uint32_t divisor, ALUOp op)
    {
        ALUResult temp = {0, false, false, false, false};

        if (divisor == 0)
        {
            // Hardware Exception: Divide by zero.
            temp.V = true;
            return temp;
        }

        uint32_t M = divisor;
        uint32_t A = 0;
        uint32_t Q = dividend;

        // 32 hardware cycles for a 32-bit division
        for (int i = 0; i < 32; i++)
        {
            bool a_is_negative = (A >> 31) & 1;

            uint32_t q_msb = (Q >> 31) & 1;
            A = (A << 1) | q_msb;
            Q = (Q << 1);

            if (a_is_negative)
            {
                A = koggeStoneAdder(A, M, 0).res;
            }
            else
            {
                A = koggeStoneAdder(A, ~M, 1).res;
            }

            if (((A >> 31) & 1) == 0)
            {
                Q |= 1;
            }
        }

        // Final Restoration Cycle to fix a negative remainder
        if ((A >> 31) & 1)
        {
            A = koggeStoneAdder(A, M, 0).res;
        }

        // THE HARDWARE MULTIPLEXER (DIV vs MOD)
        if (op == OP_DIV)
        {
            temp.res = Q; // OP_DIV requests the Quotient
        }
        else if (op == OP_MOD)
        {
            temp.res = A; // OP_MOD requests the Remainder
        }

        temp.Z = (temp.res == 0);
        temp.N = (temp.res >> 31) & 1;
        temp.C = false;

        return temp;
    }
};