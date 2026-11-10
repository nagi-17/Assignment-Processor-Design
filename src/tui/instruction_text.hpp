#ifndef RISC201_TUI_INSTRUCTION_TEXT_HPP
#define RISC201_TUI_INSTRUCTION_TEXT_HPP

// ---------------------------------------------------------------------------
// Turns a raw 32-bit instruction word - or the control signals carried by a
// pipeline latch - into a short, readable mnemonic for the TUI.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "debugger.hpp"

namespace tui {

// Full assembly text for one machine word, e.g. "add r3, r1, r2".
// Returns ".word 0x........" when the opcode is unknown.
std::string decodeInstruction(std::uint32_t word);

// Best-effort mnemonic from the IS_* control fields in a snapshot stage.
// Returns an empty string when no operation signal is set.
std::string mnemonicFromControlFields(const std::vector<DebugField>& fields);

// Instruction text for a snapshot stage.  Uses the raw IR when present, then
// the program image via PC_NEXT, then the control signals.
std::string stageInstructionText(const PipelineStageSnapshot& stage,
                                 const std::vector<std::string>& program);

// Destination register number carried by a stage, or -1 when absent.
int stageDestRegister(const PipelineStageSnapshot& stage);

// Parses a program line (32 binary digits or an 8-digit hex word).
bool parseProgramWord(const std::string& text, std::uint32_t& word);

// Parses a whole loaded program image.
std::vector<std::uint32_t> parseProgramImage(const std::vector<std::string>& program);

std::string registerName(unsigned index, bool floating = false);

} // namespace tui

#endif // RISC201_TUI_INSTRUCTION_TEXT_HPP
