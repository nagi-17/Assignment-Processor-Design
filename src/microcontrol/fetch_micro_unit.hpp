#ifndef MICROCONTROL_FETCH_MICRO_UNIT_HPP
#define MICROCONTROL_FETCH_MICRO_UNIT_HPP

// ---------------------------------------------------------------------------
// IF-stage microcontrol execution unit.
//
// Microprogram: PC -> instruction-memory address, memory data -> IR,
// PC+1 -> PC_NEXT, PC+1 -> PC.  Trap / eret / branch redirection and stalls are
// sequencer overrides applied before the fetch microprogram runs.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "microcontrol_execution_unit.hpp"
#include "pipeline_latches.hpp"

namespace mc {

struct FetchOut {
    LatchIFID latch{};
    bool hold{false};       // stall: leave the existing IF/ID latch untouched
    bool fetched{false};
};

class FetchMicroControlUnit : public MicrocontrolExecutionUnit {
public:
    FetchMicroControlUnit(const MicroprogramStore& store,
                          const MicroInstructionMemory& imem)
        : MicrocontrolExecutionUnit(store, Stage::Fetch), imem_(imem) {}

    FetchOut clock(bool stall, bool flush, bool redirect,
                   uint32_t redirect_target) {
        FetchOut out;
        imem_ok_ = true;

        if (redirect) {
            out_.reset();
            pc_ = redirect_target;
            out.latch = out_;
            return out;
        }
        if (flush) {
            out_.reset();
            out.latch = out_;
            return out;
        }
        if (stall) {
            out.hold = true;
            return out;
        }
        if (pc_ >= imem_.size()) {
            out_.reset();
            out.latch = out_;
            return out;
        }

        static constexpr uint16_t kFetchKey = 0x7FFF;
        const MicroProgram* program = store_.find(Stage::Fetch, kFetchKey);
        if (!program) {
            out_.reset();
            out.latch = out_;
            return out;
        }

        runProgram(*program);
        out.latch = out_;
        out.fetched = out_.valid;
        return out;
    }

    uint32_t pc() const { return pc_; }
    void setPC(uint32_t pc) { pc_ = pc; }

protected:
    void apply(const Microinstruction& mi) override {
        uint32_t value = readSource(mi.src);
        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);

        if (mi.dests & D_FETCH_PC)  fetch_pc_ = value;
        if (mi.dests & D_IMEM_ADDR) imem_addr_ = value;
        if (mi.dests & D_IR)        ir_ = value;
        if (mi.dests & D_PC_NEXT)   pc_next_ = value;
        if (mi.dests & D_PC)        pc_ = value;
    }

    void commit() override {
        out_.pc = fetch_pc_;
        out_.ir = ir_;
        out_.pc_next = pc_next_;
        out_.valid = imem_ok_;
    }

    void resetStage() override {
        pc_ = 0;
        fetch_pc_ = pc_next_ = ir_ = imem_addr_ = 0;
        out_.reset();
        imem_ok_ = true;
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::Pc:      return pc_;
            case BusDriver::PcPlus1: return pc_ + 1u;
            case BusDriver::ImemOut: {
                uint32_t ir = 0;
                imem_ok_ = imem_.read(imem_addr_, ir);
                return ir;
            }
            case BusDriver::Zero:    return 0;
            default:                 return 0;
        }
    }

private:
    const MicroInstructionMemory& imem_;
    uint32_t pc_{0};
    uint32_t fetch_pc_{0};
    uint32_t pc_next_{0};
    uint32_t ir_{0};
    uint32_t imem_addr_{0};
    bool     imem_ok_{true};
    LatchIFID out_{};
};

} // namespace mc

#endif // MICROCONTROL_FETCH_MICRO_UNIT_HPP
