#ifndef OPERAND_FETCH_HPP
#define OPERAND_FETCH_HPP

// ---------------------------------------------------------------------------
// Operand Fetch (ID) stage for RISC201.
//
// Works for both the 4-stage and 6-stage pipelines: IF/ID and ID/EX latches
// are identical in both designs. Select the latch header at build time:
//
//     g++ -DFOUR_STAGE_PIPELINE ...     -> four_stage_latches.hpp
//     g++ ...                           -> six_stage_latches.hpp (default)
//
// instruction_fetch.hpp must use the SAME selection (both latch headers
// define IF_ID_Latch / ID_EX_Latch, so they can never be included together).
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include "control_signals.hpp"

#ifdef FOUR_STAGE_PIPELINE
#include "four_stage_latches.hpp"
#else
#include "six_stage_latches.hpp"
#endif

class MicroprogrammedControlUnit;   // defined in microcontrol_unit.hpp

// Which registers an instruction reads. "A" feeds op1, "B" feeds op2.
// The hazard / data-lock unit uses this to decide whether to stall.
struct OperandSources {
    bool    uses_a{false};
    bool    uses_b{false};
    uint8_t a{0};
    uint8_t b{0};
};

class OperandFetchStage {
public:
    static constexpr std::size_t kNumRegs = 16;          // r0 - r15
    using RegArray = std::array<uint32_t, kNumRegs>;

    OperandFetchStage() = default;

    // One clock edge of the ID stage.
    //   in        : IF/ID latch (read only)
    //   out       : ID/EX latch (written)
    //   regs      : register file; WB must already have written this cycle
    //               (run stages in reverse order: WB ... ID, IF) so the
    //               write-then-read behaviour comes for free.
    //   is_stall  : from hazard unit -> insert a bubble into ID/EX, keep IF/ID
    //   is_flush  : from branch/trap logic -> squash ID/EX
    void clockNext(const IF_ID_Latch& in,
                   ID_EX_Latch& out,
                   const RegArray& regs,
                   const MicroprogrammedControlUnit& mcu,
                   bool is_stall,
                   bool is_flush);

    // Source registers of the instruction currently in IF/ID (for the hazard
    // unit, called BEFORE clockNext). Returns "no sources" for a bubble or an
    // illegal opcode.
    static OperandSources peekSources(const IF_ID_Latch& in,
                                      const MicroprogrammedControlUnit& mcu);

    // Same, from a raw instruction word and its decoded control word.
    static OperandSources sourcesOf(uint32_t ir, const ControlWord& cw);

    // Branch / call target in instruction-index units.
    // ID/EX carries pc_next = pc + 1, and immx holds the offset in
    // instruction units, so target = (pc_next - 1) + immx.
    static uint32_t branchTarget(uint32_t pc_next, uint32_t immx);

    // Illegal-opcode reporting. The trap unit should poll this after each
    // cycle, raise a fault, then call clearIllegal().
    bool     illegalInstruction() const { return illegal_; }
    uint32_t illegalPC() const          { return illegal_pc_; }
    uint32_t illegalIR() const          { return illegal_ir_; }
    void     clearIllegal()             { illegal_ = false; }

private:
    bool     illegal_{false};
    uint32_t illegal_pc_{0};
    uint32_t illegal_ir_{0};
};

#endif