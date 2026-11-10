#ifndef MICROCONTROL_MICROINSTRUCTION_HPP
#define MICROCONTROL_MICROINSTRUCTION_HPP

// ---------------------------------------------------------------------------
// Microinstruction format for the per-stage microcontrol execution units.
//
// A Microinstruction is one micro-op of a stage-local microprogram.  The
// micro-op names a single shared-bus driver, any number of latch destinations,
// and an optional functional-unit command (ALU / FPU / memory / branch).
//
// The bus is deliberately per stage: a single global bus would serialize the
// IF, ID, EX and MEM stages and destroy the pipeline.  Each stage's
// MicrocontrolExecutionUnit owns its own 32-bit SharedBus, so several
// instructions can be in different stages in the same clock.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "control_signals.hpp"

namespace mc {

// Pipeline stage that owns a microprogram.
enum class Stage : uint8_t {
    Fetch = 0,
    Decode,
    Execute,
    Ma1,
    Ma2,
    Writeback,
    MemoryMerged        // 4-stage: MA1 + MA2 + WB in one stage
};

// Source that drives the stage's shared bus for this micro-op.
enum class BusDriver : uint8_t {
    None = 0,

    // register / latch sources already present in the stage bank
    Pc,
    PcPlus1,
    PcNext,
    PcPlusImm,
    PcNextPlusImm,
    RegA,
    RegB,
    Imm,
    AluOut,
    FpuOut,
    Mdr,
    ReadData,
    ExResult,
    CsrOut,
    Zero,
    Const1,
    Const2,
    ConstRA,

    // instruction-field extractors (used by the ID stage)
    IrRd,
    IrRs1,
    IrRs2,
    IrImmSigned16,
    IrImmBranch,
    IrImmMovu,
    IrImmMovh,
    IrImmCsr,
    IrImmFmov,

    // register-file read ports / instruction-memory output
    RfA,
    RfB,
    ImemOut
};

// Destination registers that latch the bus value for this micro-op.
enum Dest : uint32_t {
    D_NONE          = 0u,
    D_IR            = 1u << 0,
    D_PC            = 1u << 1,
    D_PC_NEXT       = 1u << 2,
    D_FETCH_PC      = 1u << 3,
    D_A             = 1u << 4,
    D_B             = 1u << 5,
    D_IMM           = 1u << 6,
    D_RD            = 1u << 7,
    D_RS1           = 1u << 8,
    D_RS2           = 1u << 9,
    D_ALU_A         = 1u << 10,
    D_ALU_B         = 1u << 11,
    D_FPU_A         = 1u << 12,
    D_FPU_B         = 1u << 13,
    D_MAR           = 1u << 14,
    D_MDR           = 1u << 15,
    D_RESULT        = 1u << 16,
    D_ST_DATA       = 1u << 17,
    D_CSR_IN        = 1u << 18,
    D_RF_WRITE      = 1u << 19,
    D_BRANCH_TARGET = 1u << 20,
    D_BRANCH_TAKEN  = 1u << 21,
    D_STACK_FAULT   = 1u << 22,
    D_IMEM_ADDR     = 1u << 23,
    D_READ_DATA     = 1u << 24
};

enum class MicroAluOp : uint8_t {
    None = 0, Add, Sub, Mul, Div, Mod, Cmp, And, Or, Xor, Not, Lsl, Lsr, Asr
};

enum class MicroFpuOp : uint8_t {
    None = 0, Fadd, Fsub, Fmul, Fdiv, Fcmp, Fmov
};

enum class MicroMemOp : uint8_t {
    None = 0, Probe, Read, Write
};

enum class BranchCond : uint8_t {
    None = 0, Eq, Ne, Gt, Lt, Always
};

// Extra non-bus side effect executed by the stage unit.
enum class MicroEffect : uint8_t {
    None = 0,
    Chk,        // stack bounds check
    CsrWrite,   // commit the CSR write
    Trap,       // ecall / ebreak / eret / wfi status
    Link        // call: write pc_next into the result register
};

enum class Seq : uint8_t {
    Next = 0,
    Repeat,
    Done
};

struct Microinstruction {
    BusDriver    src{BusDriver::None};
    uint32_t     dests{D_NONE};
    MicroAluOp   alu_op{MicroAluOp::None};
    MicroFpuOp   fpu_op{MicroFpuOp::None};
    MicroMemOp   mem_op{MicroMemOp::None};
    BranchCond   branch{BranchCond::None};
    MicroEffect  effect{MicroEffect::None};
    Seq          seq{Seq::Done};
    uint8_t      repeat{0};
    ControlWord  legacy_bits{};
    const char*  label{""};

    Microinstruction() = default;

    Microinstruction(BusDriver source, uint32_t destinations, const char* name = "",
                     Seq sequence = Seq::Done)
        : src(source), dests(destinations), seq(sequence), label(name) {}

    static Microinstruction transfer(BusDriver source, uint32_t destinations,
                                     const char* name = "") {
        return Microinstruction(source, destinations, name, Seq::Next);
    }

    static Microinstruction alu(BusDriver source, uint32_t destinations,
                                MicroAluOp op, const char* name = "") {
        Microinstruction m(source, destinations, name, Seq::Next);
        m.alu_op = op;
        return m;
    }

    static Microinstruction fpu(BusDriver source, uint32_t destinations,
                                MicroFpuOp op, const char* name = "") {
        Microinstruction m(source, destinations, name, Seq::Next);
        m.fpu_op = op;
        return m;
    }

    static Microinstruction mem(MicroMemOp op, uint32_t destinations,
                                const char* name = "") {
        Microinstruction m(BusDriver::None, destinations, name, Seq::Next);
        m.mem_op = op;
        return m;
    }

    static Microinstruction branchOp(BranchCond cond, uint32_t destinations,
                                     const char* name = "") {
        Microinstruction m(BusDriver::None, destinations, name, Seq::Next);
        m.branch = cond;
        return m;
    }

    static Microinstruction effectOp(MicroEffect eff, const char* name = "") {
        Microinstruction m(BusDriver::None, D_NONE, name, Seq::Next);
        m.effect = eff;
        return m;
    }

    static Microinstruction commit(const char* name = "commit") {
        return Microinstruction(BusDriver::None, D_NONE, name, Seq::Done);
    }

    Microinstruction& withLegacy(std::initializer_list<Signal> signals) {
        for (Signal s : signals) legacy_bits.set(static_cast<size_t>(s));
        return *this;
    }
};

using MicroProgram = std::vector<Microinstruction>;

} // namespace mc

#endif // MICROCONTROL_MICROINSTRUCTION_HPP
