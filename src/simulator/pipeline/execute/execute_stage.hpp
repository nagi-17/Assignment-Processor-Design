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
    bool     busy{false};     // EX unit is occupied by a multi-cycle operation
    StatusInputs status_in;   // filled for ControlStatusUnit::clockNext()
};

class ExecuteStage {
public:
    // Latency (in EX cycles) of the instruction held in an ID/EX latch.
    // 1 for every single-cycle instruction, and for an invalid (bubble) latch.
    static constexpr uint32_t kIntMulCycles = 4;   // Booth + Wallace tree
    static constexpr uint32_t kIntDivCycles = 16;  // radix-4 digit recurrence
    static constexpr uint32_t kFpMulCycles  = 4;
    static constexpr uint32_t kFpDivCycles  = 6;   // Newton-Raphson reciprocal

    explicit ExecuteStage(ControlStatusUnit& csu) : csu_(csu) {}

    // Advance one clock edge.  Reads id_ex, writes the pipeline-specific
    // output latch, and returns branch / status information.
    //
    // Multi-cycle operations (mul, div, mod, fmul, fdiv) are captured and held
    // for their latency; a bubble is written to the output latch for the first
    // N-1 cycles and the real result is written on cycle N.
    ExOut clockNext(const ID_EX_Latch& in, ExOutLatch& out);

    // Latency of the instruction currently sitting in `in` (1 = single cycle).
    // Defined inline so both pipeline TUs can share it without ODR clashes.
    uint32_t latency(const ID_EX_Latch& in) const {
        if (!in.is_valid)
            return 1;
        const ControlSignals& c = in.control;
        if (c[Signal::IS_MUL])
            return kIntMulCycles;
        if (c[Signal::IS_DIV] || c[Signal::IS_MOD])
            return kIntDivCycles;
        if (c[Signal::IS_FLOAT]) {
            if (c[Signal::IS_FMUL])
                return kFpMulCycles;
            if (c[Signal::IS_FDIV])
                return kFpDivCycles;
        }
        return 1;
    }

    // True while a multi-cycle operation owns the EX stage.
    bool isBusy() const { return busy_; }

    // Clear any in-flight multi-cycle operation (program reload / reset).
    void reset() {
        busy_ = false;
        remaining_ = 0;
        held_.reset();
    }

private:
    // The original single-cycle datapath (ALU / FPU / branch / CSR / traps).
    ExOut computeNormal(const ID_EX_Latch& in, ExOutLatch& out);

    ControlStatusUnit& csu_;
    ALU  alu_;
    FPU  fpu_;

    // Multi-cycle execution state
    bool        busy_{false};
    uint32_t    remaining_{0};
    ID_EX_Latch held_;   // captured operands/control of the in-flight op
};

#endif // EXECUTE_STAGE_HPP
