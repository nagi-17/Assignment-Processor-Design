#ifndef RISC201_CONTROL_ROM_VIEW_HPP
#define RISC201_CONTROL_ROM_VIEW_HPP

// Formatting helpers shared by the 4-stage and 6-stage control-ROM sessions.

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

#include "control_signals.hpp"
#include "debugger.hpp"
#include "microcontrol_unit.hpp"

namespace control_rom_view {

inline const char* signalName(std::size_t bit) {
    static const char* names[CONTROL_WORD_WIDTH] = {
        "IS_ST", "IS_LD", "IS_BEQ", "IS_BGT", "IS_BLT", "IS_BNE",
        "IS_RET", "IS_IMMEDIATE", "IS_WB", "IS_UBRANCH", "IS_CALL",
        "IS_MOVU", "IS_MOVH",
        "IS_ADD", "IS_SUB", "IS_CMP", "IS_MUL", "IS_DIV", "IS_MOD",
        "IS_LSL", "IS_LSR", "IS_ASR", "IS_OR", "IS_AND", "IS_NOT", "IS_XOR",
        "IS_WFI", "IS_ERET", "IS_CSRR", "IS_CSRW", "IS_SYSCALL", "IS_EBREAK",
        "IS_FLOAT", "IS_FADD", "IS_FSUB", "IS_FMUL", "IS_FDIV", "IS_FCMP",
        "IS_CLEAR", "IS_CHK"
    };
    return bit < CONTROL_WORD_WIDTH ? names[bit] : "?";
}

inline void printSignals(std::ostream& os, const ControlWord& word) {
    bool first = true;
    for (std::size_t i = 0; i < CONTROL_WORD_WIDTH; ++i) {
        if (!word.test(i)) continue;
        os << (first ? "" : " ") << signalName(i);
        first = false;
    }
    if (first) os << "(none)";
}

inline const char* opcodeClass(const std::string& key) {
    if (key.size() < 2) return "invalid";
    switch (key[0]) {
        case '0': return key[1] == '0' ? "00 integer" : "01 floating point";
        case '1': return key[1] == '0' ? "10 memory/system" : "11 control flow";
        default:  return "invalid";
    }
}

inline void printControlRomPipeline(std::ostream& os,
                                    const DebuggerSnapshot& snapshot) {
    os << "Pipeline latches (control-ROM view):\n";
    if (snapshot.pipeline.empty()) {
        os << "  No pipeline state is available.\n";
        return;
    }

    for (const PipelineStageSnapshot& stage : snapshot.pipeline) {
        os << "  " << std::left << std::setw(10) << stage.stage_name
           << (stage.is_valid ? "valid" : "empty/bubble") << '\n';

        std::vector<std::string> signals;
        for (const DebugField& field : stage.fields) {
            if (field.name.rfind("IS_", 0) == 0) {
                if (field.value != 0) signals.push_back(field.name);
                continue;
            }
            os << "      " << std::left << std::setw(12) << field.name
               << " = " << formatValue(field.value, field.format) << '\n';
        }
        os << "      signals      = ";
        if (signals.empty()) {
            os << "(none)";
        } else {
            for (std::size_t i = 0; i < signals.size(); ++i)
                os << (i == 0 ? "" : " ") << signals[i];
        }
        os << '\n';
    }
}

inline void printControlRomListing(std::ostream& os) {
    MicroprogrammedControlUnit rom;
    const auto& entries = rom.entries();

    std::vector<std::string> keys;
    keys.reserve(entries.size());
    for (const auto& entry : entries) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());

    os << "\nControl ROM (" << rom.size() << " entries):\n";
    for (const std::string& key : keys) {
        os << "  key " << key << "  [" << opcodeClass(key) << "] -> ";
        printSignals(os, entries.at(key));
        os << '\n';
    }
}

} // namespace control_rom_view

#endif // RISC201_CONTROL_ROM_VIEW_HPP
