#include<bits/stdc++.h>
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

    // NOTE : all >> , << are done using barrel shift, for simplicity of code we will use >> and << only.

    FPUResult execute(uint32_t operand1, uint32_t operand2, FPUOp control_signal) {
        FPUResult out = {0, false, false, false, false};

        switch (control_signal)
        {
        case OP_FADD: out.res = executeAddSub(operand1, operand2, false); break;
        case OP_FSUB: out.res = executeAddSub(operand1, operand2, true); break;
        case OP_FMUL: out.res = executeMul(operand1, operand2); break;
        case OP_FDIV: out.res = executeDiv(operand1, operand2); break;
        case OP_FCMP:
            executeCmp(operand1, operand2, out);
            return out; // Return early for CMP
        default: break;
        } 

        // Evaluate Universal Flags
        uint32_t exp = (out.res >> 23) & 0xFF;
        uint32_t mant = out.res & 0x7FFFFF;
        
        out.Z = (exp == 0 && mant == 0);             
        out.N = (out.res >> 31) & 1;                 
        out.V = (exp == 255 && mant == 0);           
        out.NaN = (exp == 255 && mant != 0);         

        return out;
    }

private:
    const uint32_t NAN_VAL = 0x7FC00000;
    const uint32_t INF_VAL = 0x7F800000;

    // barrel Shift Left Unit

    uint32_t barrelShiftLeft(uint32_t data, uint32_t shift_amount) {
        if (shift_amount >= 32) return 0;

        uint32_t result = data;

        if (shift_amount & 0b00001) result = result << 1;
        if (shift_amount & 0b00010) result = result << 2;
        if (shift_amount & 0b00100) result = result << 4;
        if (shift_amount & 0b01000) result = result << 8;
        if (shift_amount & 0b10000) result = result << 16;

        return result;
    }

    // barrel Shift Right Unit

    uint32_t barrelShiftRight(uint32_t data, uint32_t shift_amount) {
        if (shift_amount >= 32) return 0;

        uint32_t result = data;

        if (shift_amount & 0b00001) result = result >> 1;
        if (shift_amount & 0b00010) result = result >> 2;
        if (shift_amount & 0b00100) result = result >> 4;
        if (shift_amount & 0b01000) result = result >> 8;
        if (shift_amount & 0b10000) result = result >> 16;

        return result;
    }

    // CLA Adder ( koggeStoneAdder )

    uint32_t hardware_adder(uint32_t A, uint32_t B, bool Cin = 0) {
        GPBlock tree[6][32]; 
        
        // Stage 1 : Creating GP Tree
        
        // Level 0
        for(int i = 0; i < 32; i++){
            bool a_bit = (A >> i) & 1; // extracting i-th bit of A
            bool b_bit = (B >> i) & 1; // extracting i-th bit of B
            tree[0][i].g = a_bit & b_bit; // G = A & B
            tree[0][i].p = a_bit ^ b_bit; // P = A ^ B
        }

        // Levels 1 to 5
        for (int L = 1; L <= 5; L++) { // L = level
            int block_num = 32 >> L; 
            for (int i = 0; i < block_num; i++){
                
                GPBlock right = tree[L-1][2*i];     // Lower bits (1 to m)
                GPBlock left  = tree[L-1][2*i + 1]; // Upper bits (m+1 to n)
                
                // G_out = G_{m+1,n} | P_{m+1,n} & G_{1,m}
                tree[L][i].g = left.g | (left.p & right.g);
                
                // P_out = P_{m+1,n} & P_{1,m}
                tree[L][i].p = left.p & right.p;
            }
        }

        // Stage 2 : Carry Propagation 
        bool C[6][32] = {0}; // array to store C of each block
        C[5][0] = Cin;

        // Propagate carries top-down to lower levels
        for(int L = 5; L >= 1; L--){
            int block_num = 32 >> L;
            for(int i = 0; i < block_num; i++){
                GPBlock right = tree[L-1][2*i];
                
                // Right child receives the same input carry as its parent block
                C[L-1][2*i] = C[L][i];
                
                // Left child receives the output carry of the right child:
                // C_out = G | P & C_in
                C[L-1][2*i + 1] = right.g | (right.p & C[L][i]);
            }
        }

        // final result
        uint32_t result = 0;
        
        // Level 0 contain a set of 2-bit ripple carry adders that compute the result bits using the correct input carry from Level 1
        for (int i = 0; i < 16; i++) {
            // Carry-in for this specific 2-bit block
            bool cin_block = C[1][i];

            // Bit 0 of the 2-bit RC Adder
            bool a0 = (A >> (2*i)) & 1;
            bool b0 = (B >> (2*i)) & 1;
            bool sum0 = a0 ^ b0 ^ cin_block;

            // carry from first adder of 2-bit adder
            bool cout0 = (a0 & b0) | ((a0 ^ b0) & cin_block);

            // Bit 1 of the 2-bit RC Adder
            bool a1 = (A >> (2*i + 1)) & 1;
            bool b1 = (B >> (2*i + 1)) & 1;
            bool sum1 = a1 ^ b1 ^ cout0;

            // making result (using 1U to prevent signed bit-shift overflow warnings)
            if(sum0) result |= (1U << (2*i));
            if(sum1) result |= (1U << (2*i + 1));
        }

        return result;
    }

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

    // Rounding and Normalizing

    uint32_t roundAndPack(uint32_t sign, int32_t exp, uint32_t mant, bool G, bool R, bool S) {
        // G - 1st discarded bit
        // R - 2nd discarded bit
        // S - OR of all remaining bits

        // Round to nearest, ties to even
        if (G && (R || S || (mant & 1))) {
            mant = hardware_adder(mant, 1, false); 
            if (mant & (1 << 24)) { // Overflowed the 24-bit mantissa width 
                mant >>= 1;
                exp = hardware_adder(exp, 1, false); 
            }
        }
        
        mant &= 0x7FFFFF; // remove the implicit leading 1

        if (exp >= 255) return (sign << 31) | INF_VAL; // Overflow
        if (exp <= 0) return (sign << 31);             // Underflow

        return (sign << 31) | (exp << 23) | mant;
    }

    // ADD / SUB UNIT

    uint32_t executeAddSub(uint32_t a, uint32_t b, bool is_sub) {
        FloatData A(a), B(b); // from 32 bit to sign, exp, mantissa(with leading 1) form
        if (is_sub) B.sign ^= 1;

        if (A.isNaN || B.isNaN) return NAN_VAL;
        // anyone is NAN return NAN
        if (A.isInf && B.isInf) return (A.sign == B.sign) ? (A.sign << 31 | INF_VAL) : NAN_VAL;
        // inf + inf = inf , inf - inf = NAN
        if (A.isInf) return (A.sign << 31) | INF_VAL;
        if (B.isInf) return (B.sign << 31) | INF_VAL;
        // anyone is inf then return that inf with sign
        if (A.isZero && B.isZero) return (A.sign == B.sign) ? (A.sign << 31) : 0;
        // '0' cases handling
        if (A.isZero) return (B.sign << 31) | (B.exp << 23) | (B.mant & 0x7FFFFF);
        if (B.isZero) return (A.sign << 31) | (A.exp << 23) | (A.mant & 0x7FFFFF);

        uint32_t mantA = A.mant << 3; // contains leading 1 also, << 3 for GRS bits
        uint32_t mantB = B.mant << 3;
        int32_t expA = A.exp, expB = B.exp;
        uint32_t signA = A.sign, signB = B.sign;

        int32_t expDiffForSwap = hardware_adder(expA, ~expB, true);
        int32_t mantDiffForSwap = hardware_adder(mantA, ~mantB, true);

        // swapping to get larger first
        if ((expDiffForSwap < 0) || (expDiffForSwap == 0 && mantDiffForSwap < 0)) {
            std::swap(expA, expB); 
            std::swap(mantA, mantB);
            std::swap(signA, signB);
        }

        int32_t expDiff = hardware_adder(expA, ~expB, true);
        if (expDiff > 0) {
            uint32_t sticky = 0; // initialise 
            if (expDiff >= 27) { // expDiff >= 27 , mantB entirely vanish , only stick bit left
                sticky = (mantB != 0);
                mantB = 0;
            } 
            else {
                sticky = (mantB & ((1U << expDiff) - 1)) != 0;
                mantB >>= expDiff;
            }
            mantB |= sticky; // precision preserved
        }

        uint32_t resMant = 0;
        int32_t resExp = expA;
        uint32_t resSign = signA;

        if (signA == signB) { 
            resMant = hardware_adder(mantA, mantB, false);
            if (resMant & (1 << 27)) { // overflow condition
                resMant = (resMant >> 1) | (resMant & 1); // right shift & sticky bit adjustment
                resExp = hardware_adder(resExp, 1, false);// exp++
            }
        } 
        else { 
            resMant = hardware_adder(mantA, ~mantB, true);
            if (resMant == 0) return 0;
            
            while ((resMant & (1 << 26)) == 0 && resExp > 0) { // shift leading 1 to bit 26
                resMant <<= 1;
                resExp = hardware_adder(resExp, ~1u, true);
            }
        }

        bool G = (resMant >> 2) & 1; // 2nd bit
        bool R = (resMant >> 1) & 1; // 1st bit
        bool S = resMant & 1;        // 0th bit
        resMant >>= 3;

        return roundAndPack(resSign, resExp, resMant, G, R, S);
    }

    // MULTIPLICATION UNIT

    uint32_t executeMul(uint32_t a, uint32_t b) {
        FloatData A(a), B(b); // extract the sign, exp, mantissa 
        uint32_t resSign = A.sign ^ B.sign;

        if (A.isNaN || B.isNaN) return NAN_VAL;
        if ((A.isInf && B.isZero) || (A.isZero && B.isInf)) return NAN_VAL;
        // inf * 0 = NAN
        if (A.isInf || B.isInf) return (resSign << 31) | INF_VAL;
        // return inf with appropriate sign
        if (A.isZero || B.isZero) return (resSign << 31);
        // return +-0

        int32_t sumExp = hardware_adder(A.exp, B.exp, false);
        int32_t resExp = hardware_adder(sumExp, ~127u, true); // offset adjustment

        // Hardware Multiplier Registers
        uint32_t A_reg = 0;
        uint32_t Q_reg = B.mant; // Multiplier
        uint32_t M_reg = A.mant; // Multiplicand
        
        // 24 shift-and-add cycles for 24-bit mantissas
        for (int i = 0; i < 24; i++) {
            if (Q_reg & 1) {
                A_reg = hardware_adder(A_reg, M_reg, false);
            }
            // A_reg and Q_reg behave as a single 64-bit shift register wire
            uint32_t a_lsb = A_reg & 1;
            A_reg >>= 1;
            Q_reg = (Q_reg >> 1) | (a_lsb << 31);
        }
        
        // Isolate bits [47...16] == A[23:0] + Q[31:8] into a new 32-bit variable to capture GRS cleanly
        uint32_t prod_msb = (A_reg << 8) | (Q_reg >> 24);
        bool sticky = (Q_reg & 0xFFFFFF) != 0; 

        uint32_t resMant;
        bool G, R, S;

        // A leading 1 is always found either at bit 47 or 46
        if (prod_msb & (1 << 31)) { // Leading 1 at bit 47 (prod in range [2,4))
            resMant = prod_msb >> 8; // 32 --> 24 bits
            G = (prod_msb >> 7) & 1;
            R = (prod_msb >> 6) & 1;
            S = (prod_msb & 0x3F) != 0 || sticky;
            resExp = hardware_adder(resExp, 1, false); // exp++
        } 
        else { // Leading 1 at bit 46 (prod in range [1,2))
            resMant = prod_msb >> 7; // 31--> 24 bits
            G = (prod_msb >> 6) & 1;
            R = (prod_msb >> 5) & 1;
            S = (prod_msb & 0x1F) != 0 || sticky;
        }

        return roundAndPack(resSign, resExp, resMant, G, R, S);
    }

   // DIVISION UNIT 

    uint32_t executeDiv(uint32_t a, uint32_t b) {
        FloatData A(a), B(b);
        uint32_t resSign = A.sign ^ B.sign;

        // handling edge cases
        if (A.isNaN || B.isNaN) return NAN_VAL;
        if (A.isZero && B.isZero) return NAN_VAL;
        if (A.isInf && B.isInf) return NAN_VAL;
        if (B.isZero) return (resSign << 31) | INF_VAL;
        if (A.isZero) return (resSign << 31);
        if (B.isInf) return (resSign << 31);
        if (A.isInf) return (resSign << 31) | INF_VAL;

        int32_t subExp = hardware_adder(A.exp, ~B.exp, true);
        int32_t resExp = hardware_adder(subExp, 127, false); // offset adjustment

        // Division Hardware Registers
        uint32_t R_reg = A.mant; // Remainder Accumulator
        uint32_t M_reg = B.mant; // Divisor
        uint32_t Q_reg = 0;      // Quotient

        // Evaluate 28 bits of precision (24 data + Guard, Round, Sticky)
        for (int i = 0; i < 28; i++) {
            uint32_t sub = hardware_adder(R_reg, ~M_reg, true);
            
            // If subtraction didn't underflow (sign bit is 0), R_reg >= M_reg
            if ((sub >> 31) == 0) { 
                R_reg = sub;
                Q_reg = (Q_reg << 1) | 1;
            } 
            else {
                Q_reg = (Q_reg << 1);
            }
            if (i != 27) R_reg <<= 1; 
        }

        uint32_t resMant;
        bool G, R, S;

        // Quotient Leading 1 is found at bit 27 (if A >= B) or bit 26 (if A < B)
        if (Q_reg & (1 << 27)) {
            resMant = Q_reg >> 4;
            G = (Q_reg >> 3) & 1;
            R = (Q_reg >> 2) & 1;
            S = (Q_reg & 0b11) != 0 || (R_reg != 0);
        } else {
            resMant = Q_reg >> 3;
            G = (Q_reg >> 2) & 1;
            R = (Q_reg >> 1) & 1;
            S = (Q_reg & 0b1) != 0 || (R_reg != 0);
            resExp = hardware_adder(resExp, ~1u, true); // exp--
        }

        return roundAndPack(resSign, resExp, resMant, G, R, S);
    }

    // Compare UNIT

    void executeCmp(uint32_t a, uint32_t b, FPUResult& out) {
        FloatData A(a), B(b);
        out.res = 0; 
        
        if (A.isNaN || B.isNaN) {
            out.NaN = true;
            return;
        }
        if (A.isZero && B.isZero) {
            out.Z = true; 
            return;
        }
        if (A.sign != B.sign) {
            out.N = (A.sign == 1);
            return;
        }

        bool isNeg = (A.sign == 1);
        int32_t expDiff = hardware_adder(A.exp, ~B.exp, true);
        int32_t mantDiff = hardware_adder(A.mant, ~B.mant, true);

        if (expDiff != 0) {
            out.N = (expDiff < 0) ^ isNeg;
        } else if (mantDiff != 0) {
            out.N = (mantDiff < 0) ^ isNeg;
        } else {
            out.Z = true;
        }
    }
};



