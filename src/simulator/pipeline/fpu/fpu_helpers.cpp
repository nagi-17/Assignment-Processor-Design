#include "fpu.hpp"

// barrel Shift Left Unit
uint32_t FPU::barrelShiftLeft(uint32_t data, uint32_t shift_amount) {
    if (shift_amount >= 32)
        return 0;

    uint32_t result = data;

    if (shift_amount & 0b00001) result = result << 1;
    if (shift_amount & 0b00010) result = result << 2;
    if (shift_amount & 0b00100) result = result << 4;
    if (shift_amount & 0b01000) result = result << 8;
    if (shift_amount & 0b10000) result = result << 16;

    return result;
}

// barrel Shift Right Unit
uint32_t FPU::barrelShiftRight(uint32_t data, uint32_t shift_amount) {
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
uint32_t FPU::hardware_adder(uint32_t A, uint32_t B, bool Cin) {
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
        for (int i = 0; i < block_num; i++) {
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
    for(int L = 5; L >= 1; L--) {
        int block_num = 32 >> L;
        for(int i = 0; i < block_num; i++) {
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

// Rounding and Normalizing
uint32_t FPU::roundAndPack(uint32_t sign, int32_t exp, uint32_t mant, bool G, bool R, bool S) {
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