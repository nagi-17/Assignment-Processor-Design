#ifndef MICROCONTROL_UNIT_HPP
#define MICROCONTROL_UNIT_HPP

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "control_signals.hpp"
#include "isa_config.hpp"     // OPCODE_MAP: single source of truth for opcodes

// Control store key = 6-bit opcode (from OPCODE_MAP) + 1 format bit
// (0 = R-format / SYS, 1 = I-format).
//   add R -> "000001" + "0" = "0000010"      add I -> "0000011"
//
// Keys are built from OPCODE_MAP by mnemonic, so the control unit, assembler
// and disassembler can never drift apart. put() throws on a duplicate key, and
// OPCODE_MAP.at() throws if a mnemonic is missing from the ISA config.

class MicroprogrammedControlUnit {
private:
    // Stored Microprogram Memory (Control Store ROM/RAM)
    std::unordered_map<std::string, ControlWord> controlStore;

    // ------------------------------------------------------------------
    static ControlWord makeWord(std::initializer_list<Signal> signals) {
        ControlWord cw;
        for (Signal s : signals) enableSignal(cw, s);
        return cw;
    }

    static std::string keyOf(const std::string& mnemonic, bool immFormat) {
        const std::string& opc = OPCODE_MAP.at(mnemonic);   // throws if unknown
        if (opc.size() != 6)
            throw std::logic_error("Opcode of '" + mnemonic + "' is not 6 bits");
        return opc + (immFormat ? '1' : '0');
    }

    // Insert a control word; throws if the key already exists.
    void put(const std::string& key, const ControlWord& cw) {
        if (!controlStore.emplace(key, cw).second)
            throw std::logic_error("Duplicate opcode key " + key);
    }

    // R-format / SYS entry (format bit 0).
    void putR(const std::string& mn, std::initializer_list<Signal> s) {
        put(keyOf(mn, false), makeWord(s));
    }

    // I-format entry (format bit 1); IS_IMMEDIATE is added automatically.
    void putI(const std::string& mn, std::initializer_list<Signal> s) {
        ControlWord cw = makeWord(s);
        enableSignal(cw, Signal::IS_IMMEDIATE);
        put(keyOf(mn, true), cw);
    }

    // Instructions that exist in both formats (R and I) with the same signals.
    void putRI(const std::string& mn, std::initializer_list<Signal> s) {
        putR(mn, s);
        putI(mn, s);
    }

    void initializeControlStore() {
        // mov is implemented by preprocessing, so it has no entry here.

        // nop: SYS-type, whole word is 0. No control signals at all.
        put(keyOf("nop", false), ControlWord());

        // ---------------- 1. INTEGER ALU (opcode 00xxxx) ----------------
        // Both R and I formats.
        putRI("add", {Signal::IS_ADD, Signal::IS_WB});
        putRI("sub", {Signal::IS_SUB, Signal::IS_WB});
        putRI("mul", {Signal::IS_MUL, Signal::IS_WB});
        putRI("div", {Signal::IS_DIV, Signal::IS_WB});
        putRI("mod", {Signal::IS_MOD, Signal::IS_WB});
        putRI("cmp", {Signal::IS_CMP});                    // updates flags only
        putRI("and", {Signal::IS_AND, Signal::IS_WB});
        putRI("or",  {Signal::IS_OR,  Signal::IS_WB});
        putRI("xor", {Signal::IS_XOR, Signal::IS_WB});
        putRI("not", {Signal::IS_NOT, Signal::IS_WB});
        putRI("lsl", {Signal::IS_LSL, Signal::IS_WB});
        putRI("lsr", {Signal::IS_LSR, Signal::IS_WB});
        putRI("asr", {Signal::IS_ASR, Signal::IS_WB});

        // ---------------- 2. FPU (opcode 01xxxx), single format ----------------
        putR("fadd", {Signal::IS_FLOAT, Signal::IS_FADD, Signal::IS_WB});
        putR("fsub", {Signal::IS_FLOAT, Signal::IS_FSUB, Signal::IS_WB});
        putR("fmul", {Signal::IS_FLOAT, Signal::IS_FMUL, Signal::IS_WB});
        putR("fdiv", {Signal::IS_FLOAT, Signal::IS_FDIV, Signal::IS_WB});
        putR("fcmp", {Signal::IS_FLOAT, Signal::IS_FCMP});
        // fmov (F_TYPE): IS_FLOAT + IS_WB with no FADD/FSUB/FMUL/FDIV/FCMP means
        // "pass operand through" in EX. Keyed as format bit 0 - see notes.
        putR("fmov", {Signal::IS_FLOAT, Signal::IS_WB});

        // ------------- 3. MEMORY & SYSTEM (opcode 10xxxx) -------------
        putI("ld",   {Signal::IS_LD, Signal::IS_WB, Signal::IS_ADD});   // IS_ADD = address generation
        putI("st",   {Signal::IS_ST, Signal::IS_ADD});
        putI("movu", {Signal::IS_WB, Signal::IS_MOVU});
        putI("movh", {Signal::IS_WB, Signal::IS_MOVH});
        putI("csrr", {Signal::IS_CSRR, Signal::IS_WB});
        putI("csrw", {Signal::IS_CSRW});
        putR("chk",  {Signal::IS_CHK});                                 // does not touch the flags
        putR("clrz", {Signal::IS_CLEAR, Signal::IS_WB});

        // ---------------- 4. CONTROL FLOW (opcode 11xxxx) ----------------
        putI("b",    {Signal::IS_UBRANCH});
        putI("beq",  {Signal::IS_BEQ});
        putI("bne",  {Signal::IS_BNE});
        putI("bgt",  {Signal::IS_BGT});
        putI("blt",  {Signal::IS_BLT});
        putI("call", {Signal::IS_CALL, Signal::IS_UBRANCH, Signal::IS_WB});

        putR("ret",    {Signal::IS_RET, Signal::IS_UBRANCH});
        putR("ecall",  {Signal::IS_SYSCALL});
        putR("eret",   {Signal::IS_ERET});
        putR("ebreak", {Signal::IS_EBREAK});
        putR("wfi",    {Signal::IS_WFI});
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

    // Read-only view of the control ROM, for debugger / CLI listings.
    const std::unordered_map<std::string, ControlWord>& entries() const {
        return controlStore;
    }

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