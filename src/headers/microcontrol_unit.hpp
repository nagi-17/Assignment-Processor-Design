#ifndef MICROCONTROL_UNIT_HPP
#define MICROCONTROL_UNIT_HPP

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "control_signals.hpp"

// Control store key = 6-bit opcode + 1 format bit (0 = R-format, 1 = I-format).
// Example: add R = 000000|0 -> "0000000", add I = 000000|1 -> "0000001".
// Every entry goes through put(), which throws on a duplicate key, so two
// instructions can never silently share (and overwrite) the same opcode.

class MicroprogrammedControlUnit {
private:
    // Stored Microprogram Memory (Control Store ROM/RAM)
    std::unordered_map<std::string, ControlWord> controlStore;

    // Insert a control word; throws if the key already exists.
    void put(const std::string& key, const ControlWord& cw) {
        if (!controlStore.emplace(key, cw).second)
            throw std::logic_error("Duplicate opcode key " + key);
    }

    // Convenience: build the control word from a list of signals.
    void put(const std::string& key, std::initializer_list<Signal> signals) {
        ControlWord cw;
        for (Signal s : signals) enableSignal(cw, s);
        put(key, cw);
    }

    void initializeControlStore() {
        // mov is implemented by preprocessing, so it has no entry here.

        // ---------------- 1. INTEGER ALU (opcode 00xxxx) ----------------
        // add  (000000)
        put("0000000", {Signal::IS_ADD, Signal::IS_WB});
        put("0000001", {Signal::IS_ADD, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // sub  (000001)
        put("0000010", {Signal::IS_SUB, Signal::IS_WB});
        put("0000011", {Signal::IS_SUB, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // cmp  (000010)
        put("0000100", {Signal::IS_CMP});
        put("0000101", {Signal::IS_CMP, Signal::IS_IMMEDIATE});
        // mul  (000011)
        put("0000110", {Signal::IS_MUL, Signal::IS_WB});
        put("0000111", {Signal::IS_MUL, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // div  (000100)
        put("0001000", {Signal::IS_DIV, Signal::IS_WB});
        put("0001001", {Signal::IS_DIV, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // mod  (000101)
        put("0001010", {Signal::IS_MOD, Signal::IS_WB});
        put("0001011", {Signal::IS_MOD, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // lsl  (000110)
        put("0001100", {Signal::IS_LSL, Signal::IS_WB});
        put("0001101", {Signal::IS_LSL, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // lsr  (000111)
        put("0001110", {Signal::IS_LSR, Signal::IS_WB});
        put("0001111", {Signal::IS_LSR, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // and  (001000)
        put("0010000", {Signal::IS_AND, Signal::IS_WB});
        put("0010001", {Signal::IS_AND, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // or   (001001)
        put("0010010", {Signal::IS_OR, Signal::IS_WB});
        put("0010011", {Signal::IS_OR, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // xor  (001010)
        put("0010100", {Signal::IS_XOR, Signal::IS_WB});
        put("0010101", {Signal::IS_XOR, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // not  (001011)
        put("0010110", {Signal::IS_NOT, Signal::IS_WB});
        put("0010111", {Signal::IS_NOT, Signal::IS_WB, Signal::IS_IMMEDIATE});
        // asr  (001110)
        put("0011100", {Signal::IS_ASR, Signal::IS_WB});
        put("0011101", {Signal::IS_ASR, Signal::IS_WB, Signal::IS_IMMEDIATE});

        // ---------------- 2. FPU (opcode 01xxxx), R-format ----------------
        put("0100010", {Signal::IS_FLOAT, Signal::IS_FADD, Signal::IS_WB});  // fadd 010001
        put("0100100", {Signal::IS_FLOAT, Signal::IS_FSUB, Signal::IS_WB});  // fsub 010010
        put("0100110", {Signal::IS_FLOAT, Signal::IS_FMUL, Signal::IS_WB});  // fmul 010011
        put("0101000", {Signal::IS_FLOAT, Signal::IS_FDIV, Signal::IS_WB});  // fdiv 010100
        put("0101100", {Signal::IS_FLOAT, Signal::IS_FCMP});                 // fcmp 010110

        // ------------- 3. MEMORY & SYSTEM (opcode 10xxxx) -------------
        // ld   (100000, I) - IS_ADD generates the address
        put("1000001", {Signal::IS_LD, Signal::IS_IMMEDIATE, Signal::IS_WB, Signal::IS_ADD});
        // st   (100001, I)
        put("1000011", {Signal::IS_ST, Signal::IS_IMMEDIATE, Signal::IS_ADD});
        // movu (100010, I)
        put("1000101", {Signal::IS_IMMEDIATE, Signal::IS_WB, Signal::IS_MOVU});
        // movh (100011, I)
        put("1000111", {Signal::IS_IMMEDIATE, Signal::IS_WB, Signal::IS_MOVH});
        // csrr (101000, I)
        put("1010001", {Signal::IS_CSRR, Signal::IS_IMMEDIATE, Signal::IS_WB});
        // csrw (101001, I)
        put("1010011", {Signal::IS_CSRW, Signal::IS_IMMEDIATE});
        // chk  (101100, R) - does NOT set IS_CMP, so it never touches the flags
        put("1011000", {Signal::IS_CHK});
        // clrz (101101, R)
        put("1011010", {Signal::IS_CLEAR, Signal::IS_WB});

        // ---------------- 4. CONTROL FLOW (opcode 11xxxx) ----------------
        put("1100001", {Signal::IS_UBRANCH, Signal::IS_IMMEDIATE});                 // b    110000
        put("1100011", {Signal::IS_BEQ, Signal::IS_IMMEDIATE});                     // beq  110001
        put("1100101", {Signal::IS_BNE, Signal::IS_IMMEDIATE});                     // bne  110010
        put("1100111", {Signal::IS_BGT, Signal::IS_IMMEDIATE});                     // bgt  110011
        put("1101001", {Signal::IS_BLT, Signal::IS_IMMEDIATE});                     // blt  110100
        put("1110001", {Signal::IS_CALL, Signal::IS_UBRANCH, Signal::IS_WB,
                        Signal::IS_IMMEDIATE});                                     // call 111000
        put("1110010", {Signal::IS_RET, Signal::IS_UBRANCH});                       // ret  111001
        put("1111000", {Signal::IS_SYSCALL});                                       // ecall  111100
        put("1111010", {Signal::IS_ERET});                                          // eret   111101
        put("1111100", {Signal::IS_EBREAK});                                        // ebreak 111110
        put("1111110", {Signal::IS_WFI});                                           // wfi    111111
    }

public:
    MicroprogrammedControlUnit() {
        initializeControlStore();
    }

    // Allows dynamic modification of the microprogram memory (flexibility feature).
    // Deliberately overwrites, unlike the internal put().
    void updateMicrocode(const std::string& opcode, const ControlWord& newWord) {
        controlStore[opcode] = newWord;
    }

    // Number of entries in the control store.
    std::size_t size() const { return controlStore.size(); }

    // Decodes a 32-character binary instruction string into its control word.
    // Returns false for an illegal opcode or a malformed string.
    bool decodeInstruction(const std::string& instruction, ControlWord& outWord) const {
        if (instruction.length() < 7) {
            return false;
        }
        auto it = controlStore.find(instruction.substr(0, 7));
        if (it == controlStore.end()) {
            return false;   // Illegal opcode
        }
        outWord = it->second;
        return true;
    }
};

#endif