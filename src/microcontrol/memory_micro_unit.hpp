#ifndef MICROCONTROL_MEMORY_MICRO_UNIT_HPP
#define MICROCONTROL_MEMORY_MICRO_UNIT_HPP

// ---------------------------------------------------------------------------
// MA1 / MA2 / WB microcontrol execution units and the 4-stage merged
// MEM/WB unit.  They share one simple data memory for the model.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>

#include "microcontrol_execution_unit.hpp"
#include "pipeline_latches.hpp"
#include "register_file.hpp"

namespace mc {

class DataMemory {
public:
    bool probe(uint32_t, bool) const { return true; }

    uint32_t read(uint32_t addr) const {
        auto it = memory_.find(addr);
        return it == memory_.end() ? 0u : it->second;
    }

    void write(uint32_t addr, uint32_t value) { memory_[addr] = value; }
    void reset() { memory_.clear(); }

private:
    std::map<uint32_t, uint32_t> memory_;
};

// ---------------------------------------------------------------------------

class Ma1MicroControlUnit : public MicrocontrolExecutionUnit {
public:
    Ma1MicroControlUnit(const MicroprogramStore& store, DataMemory& memory)
        : MicrocontrolExecutionUnit(store, Stage::Ma1), memory_(memory) {}

    bool clock(const LatchMem& in, LatchMem& out, bool& m1_hit) {
        in_ = &in;
        out_ = &out;
        m1_hit = true;
        bank_ = MaBank{};

        if (!in.valid) {
            out.reset();
            return false;
        }

        const MicroProgram* program = store_.find(Stage::Ma1, in.micro_key);
        if (!program) {
            out.reset();
            return false;
        }

        runProgram(*program);
        m1_hit = bank_.m1_hit;
        return true;
    }

protected:
    void apply(const Microinstruction& mi) override {
        uint32_t value = readSource(mi.src);
        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);

        if (mi.dests & D_MAR) bank_.mar = value;

        if (mi.mem_op == MicroMemOp::Probe) {
            const bool is_mem = in_ && (in_->is_ld || in_->is_st);
            bank_.m1_hit = memory_.probe(bank_.mar, is_mem);
        }
    }

    void commit() override {
        if (!out_ || !in_) return;
        out_->reset();
        out_->micro_key = in_->micro_key;
        out_->result = in_->result;
        out_->st_data = in_->st_data;
        out_->pc_next = in_->pc_next;
        out_->branch_target = in_->branch_target;
        out_->rd = in_->rd;
        out_->is_ld = in_->is_ld;
        out_->is_st = in_->is_st;
        out_->is_wb = in_->is_wb;
        out_->is_clear = in_->is_clear;
        out_->is_call = in_->is_call;
        out_->is_ecall = in_->is_ecall;
        out_->is_ebreak = in_->is_ebreak;
        out_->is_eret = in_->is_eret;
        out_->is_wfi = in_->is_wfi;
        out_->stack_fault = in_->stack_fault;
        out_->valid = true;
    }

    void resetStage() override {
        bank_ = MaBank{};
        in_ = nullptr;
        out_ = nullptr;
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::ExResult: return in_ ? in_->result : 0u;
            default: return 0u;
        }
    }

private:
    struct MaBank {
        uint32_t mar{0};
        bool m1_hit{true};
    };

    DataMemory& memory_;
    const LatchMem* in_{nullptr};
    LatchMem* out_{nullptr};
    MaBank bank_{};
};

// ---------------------------------------------------------------------------

class Ma2MicroControlUnit : public MicrocontrolExecutionUnit {
public:
    Ma2MicroControlUnit(const MicroprogramStore& store, DataMemory& memory)
        : MicrocontrolExecutionUnit(store, Stage::Ma2), memory_(memory) {}

    bool clock(const LatchMem& in, LatchMem& out, int& stall_cycles) {
        in_ = &in;
        out_ = &out;
        stall_cycles = 0;
        bank_ = MaBank{};

        if (!in.valid) {
            out.reset();
            return false;
        }

        const MicroProgram* program = store_.find(Stage::Ma2, in.micro_key);
        if (!program) {
            out.reset();
            return false;
        }

        runProgram(*program);
        return true;
    }

protected:
    void apply(const Microinstruction& mi) override {
        if (mi.mem_op == MicroMemOp::Read) {
            bank_.mdr = memory_.read(in_ ? in_->result : 0u);
        } else if (mi.mem_op == MicroMemOp::Write) {
            if (in_ && in_->is_st)
                memory_.write(in_->result, in_->st_data);
        }

        uint32_t value = readSource(mi.src);
        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);
        if (mi.dests & D_READ_DATA) bank_.read_data = value;
    }

    void commit() override {
        if (!out_ || !in_) return;
        out_->reset();
        out_->micro_key = in_->micro_key;
        out_->result = in_->result;
        out_->st_data = in_->st_data;
        out_->pc_next = in_->pc_next;
        out_->branch_target = in_->branch_target;
        out_->read_data = bank_.read_data;
        out_->rd = in_->rd;
        out_->is_ld = in_->is_ld;
        out_->is_st = in_->is_st;
        out_->is_wb = in_->is_wb;
        out_->is_clear = in_->is_clear;
        out_->is_call = in_->is_call;
        out_->is_ecall = in_->is_ecall;
        out_->is_ebreak = in_->is_ebreak;
        out_->is_eret = in_->is_eret;
        out_->is_wfi = in_->is_wfi;
        out_->stack_fault = in_->stack_fault;
        out_->valid = true;
    }

    void resetStage() override {
        bank_ = MaBank{};
        in_ = nullptr;
        out_ = nullptr;
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::Mdr: return bank_.mdr;
            default: return 0u;
        }
    }

private:
    struct MaBank {
        uint32_t mdr{0};
        uint32_t read_data{0};
    };

    DataMemory& memory_;
    const LatchMem* in_{nullptr};
    LatchMem* out_{nullptr};
    MaBank bank_{};
};

// ---------------------------------------------------------------------------

class WbMicroControlUnit : public MicrocontrolExecutionUnit {
public:
    WbMicroControlUnit(const MicroprogramStore& store, RegisterFile& rf)
        : MicrocontrolExecutionUnit(store, Stage::Writeback), rf_(rf) {}

    bool clock(const LatchMem& in) {
        in_ = &in;
        bank_ = WbBank{};

        if (!in.valid) return false;

        const MicroProgram* program = store_.find(Stage::Writeback, in.micro_key);
        if (!program) return false;

        runProgram(*program);
        return true;
    }

protected:
    void apply(const Microinstruction& mi) override {
        uint32_t value = readSource(mi.src);
        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);
        if (mi.dests & D_RF_WRITE) bank_.write_data = value;
    }

    void commit() override {
        if (!in_ || !in_->valid) return;
        if (!in_->is_wb) return;
        if (in_->is_clear) {
            rf_.write(in_->rd, 0);
            return;
        }
        rf_.write(in_->rd, bank_.write_data);
    }

    void resetStage() override {
        bank_ = WbBank{};
        in_ = nullptr;
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::ReadData: return in_ ? in_->read_data : 0u;
            case BusDriver::ExResult: return in_ ? in_->result : 0u;
            case BusDriver::Zero:     return 0u;
            default:                  return 0u;
        }
    }

private:
    struct WbBank {
        uint32_t write_data{0};
    };

    RegisterFile& rf_;
    const LatchMem* in_{nullptr};
    WbBank bank_{};
};

// ---------------------------------------------------------------------------
// 4-stage: memory access and writeback are merged into one stage.
// ---------------------------------------------------------------------------

class MergedMemWbMicroControlUnit : public MicrocontrolExecutionUnit {
public:
    MergedMemWbMicroControlUnit(const MicroprogramStore& store,
                                DataMemory& memory, RegisterFile& rf)
        : MicrocontrolExecutionUnit(store, Stage::MemoryMerged),
          memory_(memory), rf_(rf) {}

    bool clock(const LatchMem& in, bool& m1_hit, int& stall_cycles) {
        in_ = &in;
        m1_hit = true;
        stall_cycles = 0;
        bank_ = MergedBank{};

        if (!in.valid) return false;

        const MicroProgram* program = store_.find(Stage::MemoryMerged, in.micro_key);
        if (!program) return false;

        runProgram(*program);
        m1_hit = bank_.m1_hit;
        return true;
    }

protected:
    void apply(const Microinstruction& mi) override {
        uint32_t value = readSource(mi.src);

        if (mi.src != BusDriver::None)
            bus_.drive(mi.src, value);

        if (mi.dests & D_MAR)       bank_.mar = value;
        if (mi.dests & D_READ_DATA) bank_.read_data = value;
        if (mi.dests & D_RF_WRITE)  bank_.write_data = value;

        if (mi.mem_op == MicroMemOp::Probe) {
            const bool is_mem = in_ && (in_->is_ld || in_->is_st);
            bank_.m1_hit = memory_.probe(bank_.mar, is_mem);
        } else if (mi.mem_op == MicroMemOp::Read) {
            bank_.mdr = memory_.read(bank_.mar);
        } else if (mi.mem_op == MicroMemOp::Write) {
            if (in_ && in_->is_st)
                memory_.write(bank_.mar, in_->st_data);
        }
    }

    void commit() override {
        if (!in_ || !in_->valid || !in_->is_wb) return;
        if (in_->is_clear) {
            rf_.write(in_->rd, 0);
            return;
        }
        rf_.write(in_->rd, bank_.write_data);
    }

    void resetStage() override {
        bank_ = MergedBank{};
        in_ = nullptr;
    }

    uint32_t readSource(BusDriver src) override {
        switch (src) {
            case BusDriver::ExResult: return in_ ? in_->result : 0u;
            case BusDriver::Mdr:      return bank_.mdr;
            case BusDriver::ReadData: return bank_.read_data;
            case BusDriver::Zero:     return 0u;
            default:                  return 0u;
        }
    }

private:
    struct MergedBank {
        uint32_t mar{0};
        uint32_t mdr{0};
        uint32_t read_data{0};
        uint32_t write_data{0};
        bool m1_hit{true};
    };

    DataMemory& memory_;
    RegisterFile& rf_;
    const LatchMem* in_{nullptr};
    MergedBank bank_{};
};

} // namespace mc

#endif // MICROCONTROL_MEMORY_MICRO_UNIT_HPP
