#ifndef MICROCONTROL_DECODE_MICRO_UNIT_HPP
#define MICROCONTROL_DECODE_MICRO_UNIT_HPP

// ---------------------------------------------------------------------------
// ID-stage microcontrol execution unit.
//
// Microprogram: extract rd/rs1/rs2, sign/zero-extend the immediate, read the
// register-file ports, and commit the ID/EX latch.  The operand-source rules
// are also exposed through peek() for the hazard unit.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "control_signals.hpp"
#include "microcontrol_execution_unit.hpp"
#include "pipeline_latches.hpp"
#include "register_file.hpp"

namespace mc {

struct DecodeInfo {
    bool     valid{false};
    uint16_t key{kNoMicroKey};
    bool     reads_a{false};
    bool     reads_b{false};
    uint8_t  src_a{0};
    uint8_t  src_b{0};
    bool     writes_rf{false};
    uint8_t  dst_rf{0};
    bool     uses_flags{false};
};

class DecodeMicroControlUnit : public MicrocontrolExecutionUnit {
public:
    DecodeMicroControlUnit(const MicroprogramStore& store, const RegisterFile& rf)
        : MicrocontrolExecutionUnit(store, Stage::Decode), rf_(rf) {}

    bool clock(const LatchIFID& in, LatchIDEX& out, bool stall, bool flush) {
        in_ = &in;
        out_ = &out;

        if (flush || stall || !in.valid) {
            out.reset();
            return false;
        }

        bank_.ir = in.ir;
        bank_.pc_next = in.pc_next;
        bank_.key = MicroprogramStore::keyFromIR(in.ir);
        bank_.control = store_.legacyWord(bank_.key);
        bank_.rd = bank_.rs1 = bank_.rs2 = 0;
        bank_.a = bank_.b = bank_.imm = 0;

        if (!store_.hasKey(bank_.key)) {
            illegal_ = true;
            illegal_key_ = bank_.key;
            out.reset();
            return false;
        }

        const MicroProgram* program = store_.find(Stage::Decode, bank_.key);
        if (!program) {
            out.reset();
            return false;
        }

        runProgram(*program);
        return out.valid;
    }

    DecodeInfo peek(const LatchIFID& in) const {
        DecodeInfo info;
        if (!in.valid) return info;

        const uint16_t key = MicroprogramStore::keyFromIR(in.ir);
        if (!store_.hasKey(key)) return info;
        const ControlWord cw = store_.legacyWord(key);

        const uint8_t rd  = static_cast<uint8_t>((in.ir >> 21) & 0xF);
        const uint8_t rs1 = static_cast<uint8_t>((in.ir >> 17) & 0xF);
        const uint8_t rs2 = static_cast<uint8_t>((in.ir >> 13) & 0xF);
        const bool imm = cw.test(static_cast<std::size_t>(Signal::IS_IMMEDIATE));

        auto on = [&](Signal s) { return cw.test(static_cast<std::size_t>(s)); };

        info.valid = true;
        info.key = key;
        info.writes_rf = on(Signal::IS_WB);
        info.dst_rf = on(Signal::IS_CALL) ? 15 : rd;
        info.uses_flags = on(Signal::IS_BEQ) || on(Signal::IS_BNE) ||
                          on(Signal::IS_BGT) || on(Signal::IS_BLT);

        // nop: no control signals and no operands.
        if (cw.none())
            return info;

        // fmov is F_TYPE: its immediate is embedded in the instruction and it
        // has no register source.  It is the only IS_FLOAT instruction without
        // an FPU opcode bit.
        if (on(Signal::IS_FLOAT) && !on(Signal::IS_FADD) &&
            !on(Signal::IS_FSUB) && !on(Signal::IS_FMUL) &&
            !on(Signal::IS_FDIV) && !on(Signal::IS_FCMP)) {
            return info;
        }

        if (on(Signal::IS_SYSCALL)) {
            info.reads_a = true; info.src_a = 1;
            info.reads_b = true; info.src_b = 2;
            return info;
        }
        if (on(Signal::IS_RET)) {
            info.reads_a = true; info.src_a = 15;
            return info;
        }
        if (on(Signal::IS_ST)) {
            info.reads_a = true; info.src_a = rs1;
            info.reads_b = true; info.src_b = rd;
            return info;
        }
        if (on(Signal::IS_LD) || on(Signal::IS_CSRW)) {
            info.reads_a = true; info.src_a = rs1;
            return info;
        }
        if (on(Signal::IS_CHK)) {
            info.reads_a = true; info.src_a = rs1;
            info.reads_b = true; info.src_b = rs2;
            return info;
        }
        if (on(Signal::IS_MOVU) || on(Signal::IS_MOVH) ||
            on(Signal::IS_CLEAR) || on(Signal::IS_CSRR) ||
            on(Signal::IS_WFI) || on(Signal::IS_ERET) ||
            on(Signal::IS_EBREAK) ||
            on(Signal::IS_UBRANCH) || on(Signal::IS_BEQ) ||
            on(Signal::IS_BNE) || on(Signal::IS_BGT) || on(Signal::IS_BLT) ||
            on(Signal::IS_CALL)) {
            return info;
        }
        if (on(Signal::IS_NOT)) {
            if (!imm) { info.reads_b = true; info.src_b = rs2; }
            return info;
        }

        info.reads_a = true; info.src_a = rs1;
        if (!imm) { info.reads_b = true; info.src_b = rs2; }
        return info;
    }

    bool illegal() const { return illegal_; }
    uint16_t illegalKey() const { return illegal_key_; }
    void clearIllegal() { illegal_ = false; }

protected:
    void apply(const Microinstruction& mi) override {
        const uint32_t value = readSource(mi.src);
        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);

        if (mi.dests & D_RD)  bank_.rd = static_cast<uint8_t>(value & 0xF);
        if (mi.dests & D_RS1) bank_.rs1 = static_cast<uint8_t>(value & 0xF);
        if (mi.dests & D_RS2) bank_.rs2 = static_cast<uint8_t>(value & 0xF);
        if (mi.dests & D_A)   bank_.a = value;
        if (mi.dests & D_B)   bank_.b = value;
        if (mi.dests & D_IMM) bank_.imm = value;
    }

    void commit() override {
        if (!out_) return;
        out_->reset();
        out_->micro_key = bank_.key;
        out_->pc_next = bank_.pc_next;
        out_->rd = bank_.control.test(static_cast<std::size_t>(Signal::IS_CALL))
                       ? 15
                       : bank_.rd;
        out_->a = bank_.a;
        out_->b = bank_.b;
        out_->imm = bank_.imm;
        out_->control = bank_.control;
        out_->valid = true;
    }

    void resetStage() override {
        bank_ = DecodeBank{};
        out_ = nullptr;
        in_ = nullptr;
        illegal_ = false;
    }

    uint32_t readSource(BusDriver src) override {
        const uint32_t ir = bank_.ir;
        switch (src) {
            case BusDriver::IrRd:  return (ir >> 21) & 0xF;
            case BusDriver::IrRs1: return (ir >> 17) & 0xF;
            case BusDriver::IrRs2: return (ir >> 13) & 0xF;
            case BusDriver::IrImmSigned16: return signExtend((ir >> 1) & 0xFFFF, 16);
            case BusDriver::IrImmBranch: {
                uint32_t off = signExtend(ir & 0x1FFFFFFu, 25);
                return static_cast<uint32_t>(static_cast<int32_t>(off) >> 2);
            }
            case BusDriver::IrImmMovu: return (ir >> 1) & 0xFFFF;
            case BusDriver::IrImmMovh: return ((ir >> 1) & 0xFFFF) << 16;
            case BusDriver::IrImmCsr:  return (ir >> 1) & 0xFFFF;
            case BusDriver::IrImmFmov: return ir & 0x1FFFFF;
            case BusDriver::RfA: return rf_.read(bank_.rs1);
            case BusDriver::RfB: return rf_.read(bank_.rs2);
            case BusDriver::Const1: return 1;
            case BusDriver::Const2: return 2;
            case BusDriver::ConstRA: return 15;
            case BusDriver::Zero: return 0;
            default: return 0;
        }
    }

private:
    struct DecodeBank {
        uint32_t ir{0};
        uint32_t pc_next{0};
        uint16_t key{kNoMicroKey};
        uint8_t  rd{0};
        uint8_t  rs1{0};
        uint8_t  rs2{0};
        uint32_t a{0};
        uint32_t b{0};
        uint32_t imm{0};
        ControlWord control{};
    };

    static uint32_t signExtend(uint32_t v, unsigned bits) {
        const uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        v &= mask;
        const uint32_t m = 1u << (bits - 1);
        return (v ^ m) - m;
    }

    const RegisterFile& rf_;
    DecodeBank bank_{};
    LatchIDEX* out_{nullptr};
    const LatchIFID* in_{nullptr};
    bool illegal_{false};
    uint16_t illegal_key_{kNoMicroKey};
};

} // namespace mc

#endif // MICROCONTROL_DECODE_MICRO_UNIT_HPP
