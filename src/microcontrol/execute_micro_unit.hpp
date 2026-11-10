#ifndef MICROCONTROL_EXECUTE_MICRO_UNIT_HPP
#define MICROCONTROL_EXECUTE_MICRO_UNIT_HPP

// ---------------------------------------------------------------------------
// EX-stage microcontrol execution unit.
//
// Microprograms drive the shared bus into the ALU/FPU input registers, command
// the functional unit, and commit the EX/MEM latch.  Long-latency operations
// (mul, div, mod, fmul, fdiv) keep the existing capture/bubble/commit schedule.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "alu.hpp"
#include "control_status_unit.hpp"
#include "fpu.hpp"
#include "microcontrol_execution_unit.hpp"
#include "pipeline_latches.hpp"

namespace mc {

struct ExSignals {
    bool         branch_taken{false};
    uint32_t     branch_target{0};
    bool         busy{false};
    StatusInputs status{};
};

class ExecuteMicroControlUnit : public MicrocontrolExecutionUnit {
public:
    explicit ExecuteMicroControlUnit(const MicroprogramStore& store,
                                     ControlStatusUnit& csu)
        : MicrocontrolExecutionUnit(store, Stage::Execute), csu_(csu) {}

    uint32_t latency(const LatchIDEX& in) const {
        if (!in.valid) return 1;
        const ControlWord& cw = in.control;
        auto on = [&](Signal s) { return cw.test(static_cast<std::size_t>(s)); };
        if (on(Signal::IS_MUL)) return 4;
        if (on(Signal::IS_DIV) || on(Signal::IS_MOD)) return 16;
        if (on(Signal::IS_FLOAT)) {
            if (on(Signal::IS_FMUL)) return 4;
            if (on(Signal::IS_FDIV)) return 6;
        }
        return 1;
    }

    bool isBusy() const { return busy_; }

    ExSignals clock(const LatchIDEX& in, LatchMem& out) {
        ExSignals result;

        if (!in.valid) {
            out.reset();
            sig_ = result;
            return result;
        }

        if (!busy_) {
            const uint32_t cycles = latency(in);
            if (cycles > 1) {
                held_ = in;
                remaining_ = cycles;
                busy_ = true;
            } else {
                runInstruction(in, out);
                return sig_;
            }
        }

        if (--remaining_ > 0) {
            out.reset();
            result.busy = true;
            sig_ = result;
            return result;
        }

        busy_ = false;
        runInstruction(held_, out);
        return sig_;
    }

    bool stackFault() const { return bank_.stack_fault; }

protected:
    void apply(const Microinstruction& mi) override {
        uint32_t value = 0;

        if (mi.src == BusDriver::AluOut) {
            ALUResult r = alu_.execute(bank_.alu_a, bank_.alu_b, toAlu(mi.alu_op));
            bank_.alu_out = r.res;
            if (mi.alu_op == MicroAluOp::Cmp) {
                bank_.flag_write = true;
                bank_.flag_equal = r.Z;
                bank_.flag_greater = !r.Z && !r.N;
            }
            value = r.res;
        } else if (mi.src == BusDriver::FpuOut) {
            if (mi.fpu_op == MicroFpuOp::Fmov) {
                value = bank_.imm;
            } else {
                FPUResult r = fpu_.execute(bank_.fpu_a, bank_.fpu_b, toFpu(mi.fpu_op));
                bank_.fpu_out = r.res;
                if (mi.fpu_op == MicroFpuOp::Fcmp) {
                    bank_.flag_write = true;
                    bank_.flag_equal = r.Z;
                    bank_.flag_greater = !r.Z && !r.N;
                }
                value = r.res;
            }
        } else if (mi.src == BusDriver::CsrOut) {
            uint32_t v = 0;
            csu_.readCsr(bank_.imm, v);
            bank_.csr_out = v;
            value = v;
        } else {
            value = readSource(mi.src);
        }

        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);

        if (mi.dests & D_ALU_A)    bank_.alu_a = value;
        if (mi.dests & D_ALU_B)    bank_.alu_b = value;
        if (mi.dests & D_FPU_A)    bank_.fpu_a = value;
        if (mi.dests & D_FPU_B)    bank_.fpu_b = value;
        if (mi.dests & D_RESULT)   bank_.result = value;
        if (mi.dests & D_ST_DATA)  bank_.st_data = value;
        if (mi.dests & D_BRANCH_TARGET) bank_.branch_target = value;
        if (mi.dests & D_CSR_IN)   bank_.csr_in = value;
        if (mi.dests & D_MAR)      bank_.mar = value;

        if (mi.branch != BranchCond::None) {
            switch (mi.branch) {
                case BranchCond::Eq:     bank_.branch_taken = bank_.cond_e; break;
                case BranchCond::Ne:     bank_.branch_taken = !bank_.cond_e; break;
                case BranchCond::Gt:     bank_.branch_taken = bank_.cond_gt; break;
                case BranchCond::Lt:     bank_.branch_taken = !bank_.cond_gt && !bank_.cond_e; break;
                case BranchCond::Always: bank_.branch_taken = true; break;
                default: break;
            }
        }

        if (mi.effect == MicroEffect::Chk)
            bank_.stack_fault = (bank_.a >= bank_.b);
        if (mi.effect == MicroEffect::CsrWrite)
            bank_.csr_write = true;
        if (mi.effect == MicroEffect::Trap)
            bank_.trap = true;
        if (mi.effect == MicroEffect::Link)
            bank_.result = bank_.pc_next;
    }

    void commit() override {
        if (!out_) return;

        const ControlWord& cw = bank_.control;
        auto on = [&](Signal s) { return cw.test(static_cast<std::size_t>(s)); };

        out_->reset();
        out_->micro_key = bank_.micro_key;
        out_->result = bank_.result;
        out_->st_data = bank_.st_data;
        out_->pc_next = bank_.pc_next;
        out_->branch_target = bank_.branch_target;
        out_->branch_taken = bank_.branch_taken;
        out_->rd = bank_.rd;
        out_->is_ld = on(Signal::IS_LD);
        out_->is_st = on(Signal::IS_ST);
        out_->is_wb = on(Signal::IS_WB);
        out_->is_clear = on(Signal::IS_CLEAR);
        out_->is_call = on(Signal::IS_CALL);
        out_->is_ecall = on(Signal::IS_SYSCALL);
        out_->is_ebreak = on(Signal::IS_EBREAK);
        out_->is_eret = on(Signal::IS_ERET);
        out_->is_wfi = on(Signal::IS_WFI);
        out_->stack_fault = bank_.stack_fault;
        out_->valid = true;

        if (bank_.csr_write || on(Signal::IS_CSRW))
            csu_.writeCsr(bank_.imm, bank_.csr_in);

        sig_ = ExSignals{};
        sig_.branch_taken = bank_.branch_taken;
        sig_.branch_target = bank_.branch_target;
        sig_.status.pc = (bank_.pc_next > 0) ? bank_.pc_next - 1u : 0u;
        sig_.status.is_ecall = on(Signal::IS_SYSCALL);
        sig_.status.is_ebreak = on(Signal::IS_EBREAK);
        sig_.status.is_eret = on(Signal::IS_ERET);
        sig_.status.is_wfi = on(Signal::IS_WFI);
        sig_.status.chk_fault = bank_.stack_fault;
        sig_.status.flag_write = bank_.flag_write;
        sig_.status.flag_equal = bank_.flag_equal;
        sig_.status.flag_greater = bank_.flag_greater;
    }

    void resetStage() override {
        busy_ = false;
        remaining_ = 0;
        held_.reset();
        bank_ = ExBank{};
        out_ = nullptr;
        sig_ = ExSignals{};
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::RegA:       return bank_.a;
            case BusDriver::RegB:       return bank_.b;
            case BusDriver::Imm:        return bank_.imm;
            case BusDriver::PcNext:     return bank_.pc_next;
            case BusDriver::PcPlusImm:  return bank_.pc_next - 1u + bank_.imm;
            case BusDriver::PcNextPlusImm: return bank_.pc_next + bank_.imm;
            case BusDriver::Zero:       return 0;
            default:                    return 0;
        }
    }

private:
    struct ExBank {
        uint16_t micro_key{kNoMicroKey};
        uint32_t a{0}, b{0}, imm{0}, pc_next{0};
        uint8_t  rd{0};
        ControlWord control{};

        uint32_t alu_a{0}, alu_b{0}, alu_out{0};
        uint32_t fpu_a{0}, fpu_b{0}, fpu_out{0};
        uint32_t result{0}, st_data{0}, mar{0};
        uint32_t branch_target{0};
        uint32_t csr_in{0}, csr_out{0};
        bool branch_taken{false};
        bool stack_fault{false};
        bool csr_write{false};
        bool trap{false};
        bool cond_e{false};
        bool cond_gt{false};
        bool flag_write{false};
        bool flag_equal{false};
        bool flag_greater{false};
    };

    static ALU::ALUOp toAlu(MicroAluOp op) {
        switch (op) {
            case MicroAluOp::Add: return ALU::OP_ADD;
            case MicroAluOp::Sub: return ALU::OP_SUB;
            case MicroAluOp::Mul: return ALU::OP_MUL;
            case MicroAluOp::Div: return ALU::OP_DIV;
            case MicroAluOp::Mod: return ALU::OP_MOD;
            case MicroAluOp::Cmp: return ALU::OP_CMP;
            case MicroAluOp::And: return ALU::OP_AND;
            case MicroAluOp::Or:  return ALU::OP_OR;
            case MicroAluOp::Xor: return ALU::OP_XOR;
            case MicroAluOp::Not: return ALU::OP_NOT;
            case MicroAluOp::Lsl: return ALU::OP_LSL;
            case MicroAluOp::Lsr: return ALU::OP_LSR;
            case MicroAluOp::Asr: return ALU::OP_ASR;
            default:              return ALU::OP_ADD;
        }
    }

    static FPU::FPUOp toFpu(MicroFpuOp op) {
        switch (op) {
            case MicroFpuOp::Fadd: return FPU::OP_FADD;
            case MicroFpuOp::Fsub: return FPU::OP_FSUB;
            case MicroFpuOp::Fmul: return FPU::OP_FMUL;
            case MicroFpuOp::Fdiv: return FPU::OP_FDIV;
            case MicroFpuOp::Fcmp: return FPU::OP_FCMP;
            default:               return FPU::OP_FADD;
        }
    }

    void runInstruction(const LatchIDEX& in, LatchMem& out) {
        out_ = &out;
        bank_ = ExBank{};
        bank_.micro_key = in.micro_key;
        bank_.a = in.a;
        bank_.b = in.b;
        bank_.imm = in.imm;
        bank_.pc_next = in.pc_next;
        bank_.rd = in.rd;
        bank_.control = in.control;
        bank_.cond_e = csu_.flagEqual();
        bank_.cond_gt = csu_.flagGreater();

        const MicroProgram* program = store_.find(Stage::Execute, in.micro_key);
        if (!program) {
            out.reset();
            return;
        }
        runProgram(*program);
    }

    ControlStatusUnit& csu_;
    ALU alu_;
    FPU fpu_;
    bool busy_{false};
    uint32_t remaining_{0};
    LatchIDEX held_{};
    LatchMem* out_{nullptr};
    ExSignals sig_{};
    ExBank bank_{};
};

} // namespace mc

#endif // MICROCONTROL_EXECUTE_MICRO_UNIT_HPP
