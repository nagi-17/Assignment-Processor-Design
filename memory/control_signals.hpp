#ifndef CONTROL_SIGNALS_HPP
#define CONTROL_SIGNALS_HPP

#include <bitset>
#include <cstddef>

// Enum mapping each control signal to a bit index in the microprogram memory
enum class Signal {
    // Basic Control, Branch & Memory (0 - 10)
    IS_ST = 0, IS_LD, IS_BEQ, IS_BGT, IS_BLT, IS_BNE, IS_RET, IS_IMMEDIATE, IS_WB, IS_UBRANCH, IS_CALL,

    // Integer ALU Signals (11 - 22)
    IS_ADD, IS_SUB, IS_CMP, IS_MUL, IS_DIV, IS_MOD, IS_LSL, IS_LSR, IS_ASR, IS_OR, IS_AND, IS_NOT,

    // Extended System & Special Signals (23 - 29)
    IS_WFI, IS_STALL, IS_ERET, IS_CSRR, IS_CSRW, IS_SYSCALL, IS_EBREAK,

    // Floating-Point Unit (FPU) Signals (30 - 35)
    IS_FLOAT, IS_FADD, IS_FSUB, IS_FMUL, IS_FDIV, IS_FCMP,

    // Secure/Utility Signal (36)
    IS_CLEAR,

    // Total Count Sentinel
    COUNT
};

constexpr size_t CONTROL_WORD_WIDTH = static_cast<size_t>(Signal::COUNT);
using ControlWord = std::bitset<CONTROL_WORD_WIDTH>;

// Helper to set individual bits cleanly
inline void enableSignal(ControlWord& cw, Signal sig) {
    cw.set(static_cast<size_t>(sig));
}

// ControlSignals wrapper struct used across pipeline latches
struct ControlSignals {
    ControlWord bits{0};

    void reset() {
        bits.reset();
    }

    // Overloaded [] for Enum indexing
    bool operator[](Signal sig) const {
        return bits[static_cast<size_t>(sig)];
    }

    typename ControlWord::reference operator[](Signal sig) {
        return bits[static_cast<size_t>(sig)];
    }

    // Overloaded [] for raw index integer fallback
    bool operator[](size_t index) const {
        return bits[index];
    }

    typename ControlWord::reference operator[](size_t index) {
        return bits[index];
    }
};

#endif // CONTROL_SIGNALS_HPP