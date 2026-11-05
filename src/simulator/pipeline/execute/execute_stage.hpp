// execute_stage.hpp
// Execute (EX) stage for the RISC201 pipeline.
//
// Works for both the 4-stage and 6-stage pipelines; select at build time:
//     g++ -DFOUR_STAGE_PIPELINE ...   -> EX_MWB_Latch
//     g++ ...                         -> EX_MA1_Latch  (default / 6-stage)
//
// The input latch (ID_EX_Latch) is identical in both designs.

#ifndef EXECUTE_STAGE_HPP
#define EXECUTE_STAGE_HPP

#include <cstdint>
#include "control_signals.hpp"
#include "control_status_unit.hpp"

#ifdef FOUR_STAGE_PIPELINE
#include "four_stage_latches.hpp"
using ExOutLatch = EX_MWB_Latch;
#else
#include "six_stage_latches.hpp"
using ExOutLatch = EX_MA1_Latch;
#endif

#include "alu.hpp"
#include "fpu.hpp"

// Output bundle produced by the execute stage each cycle.
struct ExOut {
    bool     branch_taken{false};
    uint32_t branch_target{0};
    StatusInputs status_in;   // filled for ControlStatusUnit::clockNext()
};

class ExecuteStage {
public:
    explicit ExecuteStage(ControlStatusUnit& csu) : csu_(csu) {}

    // Advance one clock edge.  Reads id_ex, writes the pipeline-specific
    // output latch, and returns branch / status information.
    ExOut clockNext(const ID_EX_Latch& in, ExOutLatch& out);

private:
    ControlStatusUnit& csu_;
    ALU  alu_;
    FPU  fpu_;
};

#endif // EXECUTE_STAGE_HPP
