// six_stage_cpu.hpp
// RISC201 six-stage pipeline CPU (IF, ID, EX, MA1, MA2, WB).
//
// Default pipeline: do NOT define FOUR_STAGE_PIPELINE.
// Latches used: IF_ID_Latch, ID_EX_Latch, EX_MA1_Latch, MA1_MA2_Latch, MA2_WB_Latch.

#pragma once
#ifndef SIX_STAGE_CPU_HPP
#define SIX_STAGE_CPU_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "control_signals.hpp"
#include "six_stage_latches.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decode.hpp"
#include "microcontrol_unit.hpp"
#include "control_status_unit.hpp"
#if __has_include("register_file.hpp")
#include "register_file.hpp"
#include "memory_system.hpp"
#include "writeback_common.hpp"
#include "six_stage_memory.hpp"
#else
#include "pipeline/memory/register_file.hpp"
#include "pipeline/memory/memory_system.hpp"
#include "pipeline/memory/writeback_common.hpp"
#include "pipeline/memory/six_stage_memory.hpp"
#endif
#include "execute_stage.hpp"

// HDU for 6-stage — included as a header-only source file.
#include "pipeline/Hazard_Detection_Unit/hazard_detection_6stage.cpp"

#include "debugger.hpp"
#include "debugger_pipeline_adapter.hpp"

// =========================================================================
//  SixStageCPU
// =========================================================================

class SixStageCPU : public DebuggerTarget {
public:
    SixStageCPU()
        : execute_(csu_)
        , wb_(&rf_)
    {
        resetMemorySystem();
    }

    // Load a binary program (one 32-char string per instruction).
    void loadProgram(const std::vector<std::string>& binary) {
        imem_.loadProgram(binary);
        fetch_.setPC(0);
        if_id_.reset();
        id_ex_.reset();
        ex_ma1_.reset();
        ma1_ma2_.reset();
        ma2_wb_.reset();
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
        m1_hit_saved_ = true;
        ex_inst_ = Instruction::make_nop();
        ma1_inst_ = Instruction::make_nop();
        ma2_inst_ = Instruction::make_nop();
        rw_inst_ = Instruction::make_nop();
    }

    // ----- DebuggerTarget interface ----------------------------------------

    bool stepCycle() override {
        if (halted_ || stopped_) return false;

        // ---- 0. Decode the instruction in IF/ID for the HDU ----
        Instruction id_inst = if_id_.is_valid ? Instruction::decode(if_id_.ir) : Instruction::make_nop();

        // ---- 1. CSU clock ------------------------------------------------
        StatusSignals ss = csu_.clockNext(pending_status_);
        last_status_ = ss;
        pending_status_ = StatusInputs{};

        // ---- 2. HDU evaluate --------------------------------------------
        HazardControlSignals hcs = hdu_.evaluate(
            id_inst, ex_inst_, ma1_inst_, ma2_inst_, rw_inst_,
            last_ex_out_.branch_taken, last_ex_out_.branch_target);

        // ---- 3. WB stage ------------------------------------------------
        if (ma2_wb_.is_valid) {
            wb_.run(ma2_wb_);
            instructions_retired_++;
            if (rw_inst_.opcode == RISC201::OP_EBREAK) {
                halted_     = true;
                stopped_    = true;
                stop_reason_ = DebugStopReason::ProgramHalted;
                return false;
            }
        }

        // ---- 4. MA2 stage -----------------------------------------------
        int stall_c = 0;
        ma2_wb_ = ma2_.run(ma1_ma2_, m1_hit_saved_, stall_c);
        if (stall_c > 0) {
            stall_cycles_ += static_cast<uint64_t>(stall_c);
            total_cycles_ += static_cast<uint64_t>(stall_c);
        }

        // ---- 5. MA1 stage -----------------------------------------------
        bool m1_hit_new = true;
        ma1_ma2_ = ma1_.run(ex_ma1_, m1_hit_new);
        m1_hit_saved_ = m1_hit_new;

        // ---- 6. EX stage ------------------------------------------------
        ExOut ex_out = execute_.clockNext(id_ex_, ex_ma1_);
        pending_status_ = ex_out.status_in;
        last_ex_out_    = ex_out;

        // Shift instruction trackers for HDU next cycle
        rw_inst_  = (!ma2_wb_.is_valid) ? Instruction::make_nop() : ma2_inst_;
        ma2_inst_ = (!ma1_ma2_.is_valid) ? Instruction::make_nop() : ma1_inst_;
        ma1_inst_ = (!ex_ma1_.is_valid) ? Instruction::make_nop() : ex_inst_;
        ex_inst_  = (hcs.id_ex_bubble || !id_ex_.is_valid) ? Instruction::make_nop() : id_inst;

        // ---- 7. ID stage ------------------------------------------------
        bool id_stall = hcs.if_id_stall || hcs.id_ex_bubble;
        bool id_flush = hcs.if_id_flush;
        decode_.clockNext(if_id_, id_ex_, rf_.getAll(), mcu_, id_stall, id_flush);

        // ---- 8. IF stage ------------------------------------------------
        bool branch_taken   = ex_out.branch_taken || hcs.branch_taken;
        uint32_t branch_tgt = ex_out.branch_taken ? ex_out.branch_target : hcs.branch_pc;
        bool if_stall       = hcs.pc_stall || ss.halted;

        fetch_.clockNext(imem_, if_id_, if_stall, false,
                         branch_taken, branch_tgt,
                         ss.is_eret, ss.epc,
                         ss.is_trap, ss.trap_vector);

        // ---- 9. Stats ---------------------------------------------------
        total_cycles_++;
        if (hcs.pc_stall) stall_cycles_++;

        // ---- 10. Halt / end-of-program detection ------------------------
        if (!if_id_.is_valid && !id_ex_.is_valid && !ex_ma1_.is_valid &&
            !ma1_ma2_.is_valid && !ma2_wb_.is_valid &&
            fetch_.getPC() >= imem_.size()) {
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

        return true;
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
        appendSixStagePipeline(snap, if_id_, id_ex_, ex_ma1_, ma1_ma2_, ma2_wb_);
        return snap;
    }

    bool readMemoryWord(uint32_t addr, uint32_t& val) const override {
        auto it = mainMem.find(addr - (addr % BLOCK_SIZE));
        if (it == mainMem.end()) { val = 0; return true; }
        val = getWord(it->second, addr % BLOCK_SIZE);
        return true;
    }

    void addBreakpoint(uint32_t pc) { breakpoints_.push_back(pc); }
    void clearBreakpoints()         { breakpoints_.clear(); }

    uint64_t totalCycles()        const { return total_cycles_; }
    uint64_t stallCycles()        const { return stall_cycles_; }
    uint64_t instructionsRetired() const { return instructions_retired_; }

private:
    InstructionMemory          imem_;
    InstructionFetchStage      fetch_;
    OperandFetchStage          decode_;
    MicroprogrammedControlUnit mcu_;
    ExecuteStage               execute_;
    MA1Stage                   ma1_;
    MA2Stage                   ma2_;
    WBStage                    wb_;
    HazardDetectionUnit        hdu_;
    ControlStatusUnit          csu_;
    RegisterFile               rf_;

    IF_ID_Latch   if_id_;
    ID_EX_Latch   id_ex_;
    EX_MA1_Latch  ex_ma1_;
    MA1_MA2_Latch ma1_ma2_;
    MA2_WB_Latch  ma2_wb_;

    StatusInputs  pending_status_{};
    StatusSignals last_status_{};
    ExOut         last_ex_out_{};
    bool          m1_hit_saved_{true};

    Instruction   ex_inst_{};
    Instruction   ma1_inst_{};
    Instruction   ma2_inst_{};
    Instruction   rw_inst_{};

    uint64_t total_cycles_{0};
    uint64_t stall_cycles_{0};
    uint64_t instructions_retired_{0};

    bool halted_{false};
    bool stopped_{false};
    DebugStopReason stop_reason_{DebugStopReason::None};
    std::vector<uint32_t> breakpoints_;
};

#endif // SIX_STAGE_CPU_HPP
