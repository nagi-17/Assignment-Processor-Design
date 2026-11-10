#ifndef MICROCONTROL_MICROPROGRAMMED_CPU_HPP
#define MICROCONTROL_MICROPROGRAMMED_CPU_HPP

// ---------------------------------------------------------------------------
// Standalone RISC201 model with a microcontrol execution unit behind every
// pipeline latch.  It reuses the project's ALU, FPU, register file and
// control/status unit, but keeps its own latches, microcode store and simple
// data memory so the production pipeline sources are untouched.
//
// four_stage == true  : IF, ID, EX, MEM/WB
// four_stage == false : IF, ID, EX, MA1, MA2, WB
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "control_status_unit.hpp"
#include "decode_micro_unit.hpp"
#include "execute_micro_unit.hpp"
#include "fetch_micro_unit.hpp"
#include "memory_micro_unit.hpp"
#include "microprogram_store.hpp"
#include "pipeline_latches.hpp"
#include "register_file.hpp"

namespace mc {

class MicroprogrammedCPU {
public:
    explicit MicroprogrammedCPU(bool four_stage, bool trace = false);

    void loadProgram(const std::vector<std::string>& binary);
    void reset();

    bool step();
    void run(std::uint64_t max_cycles = 1000000);

    bool     fourStage() const          { return four_stage_; }
    bool     halted() const             { return halted_; }
    bool     wfi() const                { return wfi_; }
    uint32_t pc() const                 { return fetch_.pc(); }
    const std::array<std::uint32_t, 16>& registers() const { return rf_.getAll(); }
    std::uint64_t totalCycles() const   { return total_cycles_; }
    std::uint64_t stallCycles() const   { return stall_cycles_; }
    std::uint64_t instructionsRetired() const { return retired_; }

    bool readMemory(std::uint32_t address, std::uint32_t& value) const;

    void setTrace(bool on) { trace_ = on; }
    bool traceEnabled() const { return trace_; }

    // Read-only state accessors for the integrated simulator CLI.
    const MicroprogramStore& store() const { return store_; }
    const LatchIFID& ifID() const { return if_id_; }
    const LatchIDEX& idEX() const { return id_ex_; }
    const LatchMem& exMem() const { return ex_mem_; }
    const LatchMem& ma1Ma2() const { return ma1_ma2_; }
    const LatchMem& ma2Wb() const { return ma2_wb_; }

    const MicrocontrolExecutionUnit& fetchUnit() const { return fetch_; }
    const MicrocontrolExecutionUnit& decodeUnit() const { return decode_; }
    const MicrocontrolExecutionUnit& executeUnit() const { return execute_; }
    const MicrocontrolExecutionUnit& ma1Unit() const { return ma1_; }
    const MicrocontrolExecutionUnit& ma2Unit() const { return ma2_; }
    const MicrocontrolExecutionUnit& wbUnit() const { return wb_; }
    const MicrocontrolExecutionUnit& mergedUnit() const { return merged_; }

    void dumpRegisters() const;
    void dumpPipeline() const;
    void dumpMicro() const;
    void dumpTrace() const;

private:
    bool producerConflict(const DecodeInfo& consumer, const LatchMem& ma1,
                          const LatchMem& ma2) const;
    bool writesMatch(const LatchMem& producer, const DecodeInfo& consumer) const;
    bool writesMatchIDEX(const DecodeInfo& consumer) const;
    void printCycleHeader() const;

    bool four_stage_{false};
    bool trace_{false};

    // Functional units / storage (declared before the units that reference them)
    MicroprogramStore   store_;
    MicroInstructionMemory imem_;
    RegisterFile        rf_;
    ControlStatusUnit   csu_;
    DataMemory          dmem_;

    // Stage-local microcontrol execution units
    FetchMicroControlUnit  fetch_;
    DecodeMicroControlUnit decode_;
    ExecuteMicroControlUnit execute_;
    Ma1MicroControlUnit    ma1_;
    Ma2MicroControlUnit    ma2_;
    WbMicroControlUnit     wb_;
    MergedMemWbMicroControlUnit merged_;

    // Pipeline latches
    LatchIFID if_id_;
    LatchIDEX id_ex_;
    LatchMem  ex_mem_;
    LatchMem  ma1_ma2_;
    LatchMem  ma2_wb_;

    StatusInputs pending_status_{};

    bool     halted_{false};
    bool     wfi_{false};
    std::uint64_t total_cycles_{0};
    std::uint64_t stall_cycles_{0};
    std::uint64_t retired_{0};
};

} // namespace mc

#endif // MICROCONTROL_MICROPROGRAMMED_CPU_HPP
