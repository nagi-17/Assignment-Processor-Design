// micro_session.cpp
// EngineSession adapter exposing the microprogrammed-control-unit model to the
// production simulator's CLI.

#include "engine_session.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

#include "control_rom_view.hpp"
#include "microprogrammed_cpu.hpp"

namespace {

constexpr std::uint16_t kFetchKey = 0x7FFF;

const char* busDriverName(mc::BusDriver d) {
    switch (d) {
        case mc::BusDriver::None: return "none";
        case mc::BusDriver::Pc: return "PC";
        case mc::BusDriver::PcPlus1: return "PC+1";
        case mc::BusDriver::PcNext: return "PC_NEXT";
        case mc::BusDriver::PcPlusImm: return "PC+imm";
        case mc::BusDriver::PcNextPlusImm: return "PC_NEXT+imm";
        case mc::BusDriver::RegA: return "A";
        case mc::BusDriver::RegB: return "B";
        case mc::BusDriver::Imm: return "IMM";
        case mc::BusDriver::AluOut: return "ALUOut";
        case mc::BusDriver::FpuOut: return "FPUOut";
        case mc::BusDriver::Mdr: return "MDR";
        case mc::BusDriver::ReadData: return "READ_DATA";
        case mc::BusDriver::ExResult: return "EX_RESULT";
        case mc::BusDriver::CsrOut: return "CSR";
        case mc::BusDriver::Zero: return "zero";
        case mc::BusDriver::Const1: return "const1";
        case mc::BusDriver::Const2: return "const2";
        case mc::BusDriver::ConstRA: return "ra";
        case mc::BusDriver::IrRd: return "IR[rd]";
        case mc::BusDriver::IrRs1: return "IR[rs1]";
        case mc::BusDriver::IrRs2: return "IR[rs2]";
        case mc::BusDriver::IrImmSigned16: return "IR[imm16]";
        case mc::BusDriver::IrImmBranch: return "IR[offset]";
        case mc::BusDriver::IrImmMovu: return "IR[imm16]";
        case mc::BusDriver::IrImmMovh: return "IR[imm16]<<16";
        case mc::BusDriver::IrImmCsr: return "IR[csr]";
        case mc::BusDriver::IrImmFmov: return "IR[imm21]";
        case mc::BusDriver::RfA: return "RF[rs1]";
        case mc::BusDriver::RfB: return "RF[rs2]";
        case mc::BusDriver::ImemOut: return "IMEM";
    }
    return "?";
}

const char* stageName(mc::Stage s) {
    switch (s) {
        case mc::Stage::Fetch: return "Fetch";
        case mc::Stage::Decode: return "Decode";
        case mc::Stage::Execute: return "Execute";
        case mc::Stage::Ma1: return "MA1";
        case mc::Stage::Ma2: return "MA2";
        case mc::Stage::Writeback: return "WB";
        case mc::Stage::MemoryMerged: return "MEM/WB";
    }
    return "?";
}

const char* seqName(mc::Seq s) {
    switch (s) {
        case mc::Seq::Next: return "next";
        case mc::Seq::Repeat: return "repeat";
        case mc::Seq::Done: return "done";
    }
    return "?";
}

std::string keyText(std::uint16_t key) {
    return key == mc::kNoMicroKey ? std::string("-")
                                  : mc::MicroprogramStore::keyString(key);
}

void printMemLatch(std::ostream& os, const char* name, const mc::LatchMem& l) {
    os << "  " << std::left << std::setw(8) << name
       << " valid=" << (l.valid ? "Y" : "-")
       << " key=" << keyText(l.micro_key)
       << " res=0x" << std::hex << std::setw(8) << std::setfill('0') << l.result
       << std::dec << std::setfill(' ')
       << " rd=r" << static_cast<int>(l.rd)
       << " ld=" << l.is_ld << " st=" << l.is_st << " wb=" << l.is_wb
       << " br=" << l.branch_taken;
    if (l.is_ld)
        os << " read=0x" << std::hex << l.read_data << std::dec;
    os << '\n';
}

void printLatchView(std::ostream& os, const mc::MicroprogrammedCPU& cpu) {
    os << "Pipeline latches (microprogrammed-CU view):\n";

    const mc::LatchIFID& ifid = cpu.ifID();
    os << "  IF/ID    valid=" << (ifid.valid ? "Y" : "-")
       << " pc=" << ifid.pc
       << " ir=0x" << std::hex << std::setw(8) << std::setfill('0') << ifid.ir
       << std::dec << std::setfill(' ')
       << " pc_next=" << ifid.pc_next << '\n';

    const mc::LatchIDEX& id = cpu.idEX();
    os << "  ID/EX    valid=" << (id.valid ? "Y" : "-")
       << " key=" << keyText(id.micro_key)
       << " rd=r" << static_cast<int>(id.rd)
       << " A=0x" << std::hex << std::setw(8) << std::setfill('0') << id.a
       << " B=0x" << std::setw(8) << id.b
       << " IMM=0x" << std::setw(8) << id.imm
       << std::dec << std::setfill(' ') << '\n';
    os << "      signals  = ";
    control_rom_view::printSignals(os, id.control);
    os << '\n';

    printMemLatch(os, "EX/MEM", cpu.exMem());
    if (!cpu.fourStage()) {
        printMemLatch(os, "MA1/MA2", cpu.ma1Ma2());
        printMemLatch(os, "MA2/WB", cpu.ma2Wb());
    }
}

void printUnit(std::ostream& os, const char* name,
               const mc::MicrocontrolExecutionUnit& unit) {
    os << "  " << std::left << std::setw(8) << name
       << " uPC=" << std::setw(2) << static_cast<int>(unit.microPC())
       << " MIR=" << std::setw(22)
       << (unit.mir().label ? unit.mir().label : "")
       << " bus=" << std::setw(12) << busDriverName(unit.bus().driver())
       << " value=0x" << std::hex << std::setw(8) << std::setfill('0')
       << unit.bus().read() << std::dec << std::setfill(' ')
       << " ops=" << unit.lastTrace().size()
       << " seq=" << seqName(unit.mir().seq) << '\n';
}

void printUnits(std::ostream& os, const mc::MicroprogrammedCPU& cpu) {
    os << "\nMicrocontrol execution units:\n";
    printUnit(os, "IF", cpu.fetchUnit());
    printUnit(os, "ID", cpu.decodeUnit());
    printUnit(os, "EX", cpu.executeUnit());
    if (cpu.fourStage()) {
        printUnit(os, "MEM/WB", cpu.mergedUnit());
    } else {
        printUnit(os, "MA1", cpu.ma1Unit());
        printUnit(os, "MA2", cpu.ma2Unit());
        printUnit(os, "WB", cpu.wbUnit());
    }
}

void printTraceEntry(std::ostream& os, const char* name,
                     const std::vector<mc::Microinstruction>& trace) {
    os << "  " << std::left << std::setw(8) << name << ":";
    if (trace.empty()) {
        os << " (bubble)\n";
        return;
    }
    for (const mc::Microinstruction& mi : trace)
        os << " [" << (mi.label ? mi.label : "?") << "]";
    os << '\n';
}

void printTrace(std::ostream& os, const mc::MicroprogrammedCPU& cpu) {
    os << "\nLast-clock micro-op trace:\n";
    printTraceEntry(os, "IF", cpu.fetchUnit().lastTrace());
    printTraceEntry(os, "ID", cpu.decodeUnit().lastTrace());
    printTraceEntry(os, "EX", cpu.executeUnit().lastTrace());
    if (cpu.fourStage()) {
        printTraceEntry(os, "MEM/WB", cpu.mergedUnit().lastTrace());
    } else {
        printTraceEntry(os, "MA1", cpu.ma1Unit().lastTrace());
        printTraceEntry(os, "MA2", cpu.ma2Unit().lastTrace());
        printTraceEntry(os, "WB", cpu.wbUnit().lastTrace());
    }
}

std::vector<mc::Stage> stageList(bool four_stage) {
    if (four_stage)
        return {mc::Stage::Decode, mc::Stage::Execute, mc::Stage::MemoryMerged};
    return {mc::Stage::Decode, mc::Stage::Execute, mc::Stage::Ma1,
            mc::Stage::Ma2, mc::Stage::Writeback};
}

void printProgram(std::ostream& os, mc::Stage stage, const mc::MicroProgram& p) {
    os << "    " << std::left << std::setw(9) << stageName(stage) << ":";
    for (const mc::Microinstruction& mi : p) {
        os << " [" << (mi.label ? mi.label : "?");
        if (mi.seq == mc::Seq::Done) os << " |done";
        os << ']';
    }
    os << '\n';
}

void printMicroPrograms(std::ostream& os, const mc::MicroprogrammedCPU& cpu,
                        bool full_listing) {
    const mc::MicroprogramStore& store = cpu.store();
    os << "\nMicroprograms";

    if (full_listing) {
        os << " (full store):\n";
        if (const mc::MicroProgram* fetch = store.find(mc::Stage::Fetch, kFetchKey))
            printProgram(os, mc::Stage::Fetch, *fetch);

        for (std::uint16_t key : store.keys()) {
            os << "  key " << mc::MicroprogramStore::keyString(key) << '\n';
            for (mc::Stage stage : stageList(cpu.fourStage())) {
                if (const mc::MicroProgram* p = store.find(stage, key))
                    printProgram(os, stage, *p);
            }
        }
        os << "  (" << store.programCount() << " programs, "
           << store.keyCount() << " instruction keys)\n";
        return;
    }

    os << " (in flight):\n";
    if (const mc::MicroProgram* fetch = store.find(mc::Stage::Fetch, kFetchKey))
        printProgram(os, mc::Stage::Fetch, *fetch);

    std::vector<std::uint16_t> keys;
    auto addKey = [&keys](std::uint16_t key) {
        if (key != mc::kNoMicroKey) keys.push_back(key);
    };
    addKey(cpu.idEX().micro_key);
    addKey(cpu.exMem().micro_key);
    addKey(cpu.ma1Ma2().micro_key);
    addKey(cpu.ma2Wb().micro_key);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    if (keys.empty()) {
        os << "  (no instruction in flight; use 'all' to dump the full store)\n";
        return;
    }

    for (std::uint16_t key : keys) {
        os << "  key " << mc::MicroprogramStore::keyString(key) << '\n';
        for (mc::Stage stage : stageList(cpu.fourStage())) {
            if (const mc::MicroProgram* p = store.find(stage, key))
                printProgram(os, stage, *p);
        }
    }
}

class MicroprogramSession : public EngineSession {
public:
    explicit MicroprogramSession(bool four_stage) : cpu_(four_stage) {}

    void load(const std::vector<std::string>& program) override {
        program_ = program;
        cpu_.loadProgram(program_);
    }

    bool step() override { return cpu_.step(); }
    void run(std::uint64_t max_cycles) override { cpu_.run(max_cycles); }
    void reset() override { cpu_.loadProgram(program_); }

    std::uint64_t cycles() const override { return cpu_.totalCycles(); }
    bool halted() const override { return cpu_.halted(); }

    void printView(std::ostream& os, bool full_listing) const override {
        os << "\n==== Microprogrammed-CU Pipeline ("
           << (cpu_.fourStage() ? "4-stage" : "6-stage")
           << ") - cycle " << cpu_.totalCycles() << " ====\n";
        os << "  retired=" << cpu_.instructionsRetired()
           << "  stalls=" << cpu_.stallCycles()
           << "  halted=" << (cpu_.halted() ? "yes" : "no") << "\n\n";

        printLatchView(os, cpu_);
        printUnits(os, cpu_);
        printTrace(os, cpu_);
        printMicroPrograms(os, cpu_, full_listing);
    }

private:
    mc::MicroprogrammedCPU cpu_;
    std::vector<std::string> program_;
};

} // namespace

std::unique_ptr<EngineSession> makeMicroprogramSession(bool four_stage) {
    return std::make_unique<MicroprogramSession>(four_stage);
}
