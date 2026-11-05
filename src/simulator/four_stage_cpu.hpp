// four_stage_cpu.hpp
// RISC201 four-stage pipeline CPU.
//
// Must be compiled with -DFOUR_STAGE_PIPELINE so that every header that
// switches on this macro picks the right latch types.
//
// This header is a "unity include": it pulls in all implementation files
// directly so the entire CPU compiles as a single translation unit.

#pragma once
#ifndef FOUR_STAGE_CPU_HPP
#define FOUR_STAGE_CPU_HPP

// --- pipeline selection ---------------------------------------------------
#ifndef FOUR_STAGE_PIPELINE
#  define FOUR_STAGE_PIPELINE
#endif

// --- standard library -----------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// --- project headers (order matters for forward declarations) -------------
#include "control_signals.hpp"
#include "four_stage_latches.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decode.hpp"
#include "microcontrol_unit.hpp"
#include "control_status_unit.hpp"
#if __has_include("register_file.hpp")
#include "register_file.hpp"
#include "memory_system.hpp"
#include "writeback_common.hpp"
#include "four_stage_memory.hpp"
#else
#include "pipeline/memory/register_file.hpp"
#include "pipeline/memory/memory_system.hpp"
#include "pipeline/memory/writeback_common.hpp"
#include "pipeline/memory/four_stage_memory.hpp"
#endif
#include "execute_stage.hpp"

// HDU for 4-stage — included as a header-only source file.
#include "pipeline/Hazard_Detection_Unit/hazard_detection_4stage.cpp"

#include "debugger.hpp"
#include "debugger_pipeline_adapter.hpp"

// =========================================================================
//  FourStageCPU
// =========================================================================

class FourStageCPU : public DebuggerTarget {
public:
    // Constructor — wires everything together.
    FourStageCPU()
        : execute_(csu_)
        , mwb_(&rf_)
    {
        resetMemorySystem();
    }

    // Load a binary program (one 32-char string per instruction).
    void loadProgram(const std::vector<std::string>& binary) {
        imem_.loadProgram(binary);
        // Rewind all state.
        fetch_.setPC(0);
        if_id_.reset();
        id_ex_.reset();
        ex_mwb_.reset();
        execute_.reset();
        rf_.reset();
        csu_.reset();
        resetMemorySystem();
        total_cycles_       = 0;
        stall_cycles_       = 0;
        instructions_retired_ = 0;
        halted_   = false;
        stopped_  = false;
        stop_reason_ = DebugStopReason::None;
        last_status_ = StatusSignals{};
        last_ex_out_ = ExOut{};
        pending_status_ = StatusInputs{};
        id_ex_inst_   = Instruction::make_nop();
        ex_mwb_inst_  = Instruction::make_nop();
        ex_next_inst_ = Instruction::make_nop();
    }

    // ----- DebuggerTarget interface ----------------------------------------

    bool stepCycle() override {
        if (halted_ || stopped_) return false;

        // ---- 0. Decode the instruction currently in IF/ID for the HDU ----
        Instruction id_inst = Instruction::decode(if_id_.ir);
        if (!if_id_.is_valid) id_inst = Instruction::make_nop();

        // Multi-cycle EX tracking: `ex_busy` means a long-latency op already owns
        // EX (from a previous cycle); `will_be_busy` means one captures EX now.
        bool ex_busy      = execute_.isBusy();
        bool will_be_busy = !ex_busy && execute_.latency(id_ex_) > 1;

        // Reconstruct EX and MWB instructions from their latch control word.
        // The 4-stage HDU needs decoded Instruction structs for OF (=ID) and
        // EX stages; MWB uses the saved raw ir threaded through the latches.
        // For simplicity we pass a "just-decoded" EX instruction using the
        // control word's opcode bits embedded in ex_mwb_.rd / is_* fields —
        // but the HDU only needs writes_register(), reads_src1/2(), so we
        // reconstruct a minimal Instruction.
        Instruction ex_inst  = id_ex_inst_;   // saved from last cycle
        Instruction mwb_inst = ex_mwb_inst_;  // saved from two cycles ago

        // ---- 1. CSU clock ------------------------------------------------
        StatusSignals ss = csu_.clockNext(pending_status_);
        last_status_ = ss;
        pending_status_ = StatusInputs{};   // reset for next cycle

        // ---- 2. HDU evaluate --------------------------------------------
        HazardControlSignals hcs = hdu_.evaluate(
            id_inst, ex_inst, mwb_inst,
            last_ex_out_.branch_taken, last_ex_out_.branch_target, ex_busy);

        // ---- 3. MWB (memory + writeback) --------------------------------
        bool m1_hit = true;
        int  stall_c = 0;
        mwb_.run(ex_mwb_, m1_hit, stall_c);
        if (stall_c > 0) {
            stall_cycles_ += static_cast<uint64_t>(stall_c);
            total_cycles_ += static_cast<uint64_t>(stall_c);
        }
        if (ex_mwb_.is_valid) {
            instructions_retired_++;
            if (ex_mwb_.is_Ebreak) {
                halted_     = true;
                stopped_    = true;
                stop_reason_ = DebugStopReason::ProgramHalted;
                return false;
            }
        }

        // ---- 4. EX ------------------------------------------------------
        ExOut ex_out = execute_.clockNext(id_ex_, ex_mwb_);
        pending_status_ = ex_out.status_in;
        last_ex_out_    = ex_out;

        // Save the EX/MWB tracker for the HDU next cycle (EX/MWB latch was just
        // updated above, so `ex_inst` is this-cycle's EX instruction).
        ex_mwb_inst_ = (!ex_mwb_.is_valid) ? Instruction::make_nop() : ex_inst;

        // ---- 5. ID -------------------------------------------------------
        // Hold ID/EX while the multi-cycle unit is busy (the capture cycle still
        // decodes, replacing the ID/EX latch so the op is not re-executed later).
        bool id_stall  = hcs.if_of_stall || hcs.of_ex_bubble;
        bool id_flush  = hcs.if_of_flush;
        if (!ex_busy)
            decode_.clockNext(if_id_, id_ex_, rf_.getAll(), mcu_, id_stall, id_flush);

        // ID/EX tracker = instruction that will occupy EX next cycle.
        //  * while a multi-cycle op owns EX, the tracker stays pinned to that op;
        //  * on its capture cycle the waiting ID/EX instruction is remembered;
        //  * on the final cycle the waiting instruction takes over the tracker.
        if (ex_busy) {
            if (!execute_.isBusy()) {
                id_ex_inst_ = ex_next_inst_;
                ex_next_inst_ = Instruction::make_nop();
            }
        } else if (will_be_busy) {
            ex_next_inst_ = (!id_ex_.is_valid) ? Instruction::make_nop() : id_inst;
        } else {
            id_ex_inst_ = (!id_ex_.is_valid) ? Instruction::make_nop() : id_inst;
        }

        // ---- 6. IF -------------------------------------------------------
        bool branch_taken  = ex_out.branch_taken || hcs.branch_taken;
        uint32_t branch_tgt = ex_out.branch_taken ? ex_out.branch_target : hcs.branch_pc;
        bool if_stall      = hcs.pc_stall || ss.halted;

        fetch_.clockNext(imem_, if_id_, if_stall, false,
                         branch_taken, branch_tgt,
                         ss.is_eret, ss.epc,
                         ss.is_trap, ss.trap_vector);

        // ---- 7. Stats ---------------------------------------------------
        total_cycles_++;
        if (hcs.pc_stall) stall_cycles_++;

        // ---- 8. Halt / end-of-program detection -------------------------
        // If PC is past the end and all latches are bubbles and the EX unit is
        // idle, we're done.
        if (!if_id_.is_valid && !id_ex_.is_valid && !ex_mwb_.is_valid &&
            !execute_.isBusy() && fetch_.getPC() >= imem_.size()) {
            halted_     = true;
            stopped_    = true;
            stop_reason_ = DebugStopReason::ProgramHalted;
            return false;
        }

        // Check breakpoints (simple PC match).
        for (uint32_t bp : breakpoints_) {
            if (fetch_.getPC() == bp) {
                stopped_     = true;
                stop_reason_ = DebugStopReason::Breakpoint;
                return false;
            }
        }

        return true;  // cycle completed normally
    }

    bool resume() override {
        if (stop_reason_ == DebugStopReason::Breakpoint) {
            stopped_ = false;
            return true;
        }
        return false;
    }

    DebuggerSnapshot getSnapshot() const override {
        DebuggerSnapshot snap;
        snap.pc          = fetch_.getPC();
        snap.registers   = rf_.getAll();
        snap.flag_e      = csu_.flagEqual();
        snap.flag_gt     = csu_.flagGreater();
        snap.has_flags   = true;
        snap.epc         = csu_.epc();
        snap.has_epc     = true;
        snap.cause       = csu_.cause();
        snap.has_cause   = true;
        snap.is_stopped  = stopped_;
        snap.stop_reason = stop_reason_;
        snap.total_cycles = total_cycles_;
        snap.stall_cycles = stall_cycles_;
        snap.instructions_retired = instructions_retired_;
        snap.has_stats   = true;
        appendFourStagePipeline(snap, if_id_, id_ex_, ex_mwb_);
        return snap;
    }

    bool readMemoryWord(uint32_t addr, uint32_t& val) const override {
        // Simple main-memory read (ignores cache state).
        auto it = mainMem.find(addr - (addr % BLOCK_SIZE));
        if (it == mainMem.end()) { val = 0; return true; }
        val = getWord(it->second, addr % BLOCK_SIZE);
        return true;
    }

    // ---- Extra management -----------------------------------------------
    void addBreakpoint(uint32_t pc) { breakpoints_.push_back(pc); }
    void clearBreakpoints()         { breakpoints_.clear(); }

    uint64_t totalCycles()        const { return total_cycles_; }
    uint64_t stallCycles()        const { return stall_cycles_; }
    uint64_t instructionsRetired() const { return instructions_retired_; }
    double   clockPeriodNs()      const { return CLOCK_PERIOD_NS; }

    static constexpr double CLOCK_PERIOD_NS = 2.0;

private:
    // --- pipeline components ---
    InstructionMemory       imem_;
    InstructionFetchStage   fetch_;
    OperandFetchStage       decode_;
    MicroprogrammedControlUnit mcu_;
    ExecuteStage            execute_;
    MemoryWritebackStage    mwb_;
    HazardDetectionUnit     hdu_;
    ControlStatusUnit       csu_;
    RegisterFile            rf_;

    // --- pipeline latches ---
    IF_ID_Latch   if_id_;
    ID_EX_Latch   id_ex_;
    EX_MWB_Latch  ex_mwb_;

    // --- cross-cycle state ---
    StatusInputs  pending_status_{};
    StatusSignals last_status_{};
    ExOut         last_ex_out_{};
    Instruction   id_ex_inst_{};    // decoded instruction carried from ID to EX
    Instruction   ex_mwb_inst_{};   // decoded instruction carried from EX to MWB
    Instruction   ex_next_inst_{};  // ID/EX instruction waiting behind a multi-cycle op

    // --- stats ---
    uint64_t total_cycles_{0};
    uint64_t stall_cycles_{0};
    uint64_t instructions_retired_{0};

    // --- control ---
    bool halted_{false};
    bool stopped_{false};
    DebugStopReason stop_reason_{DebugStopReason::None};
    std::vector<uint32_t> breakpoints_;
};

#endif // FOUR_STAGE_CPU_HPP
