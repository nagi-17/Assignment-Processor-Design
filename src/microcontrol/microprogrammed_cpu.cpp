#include "microprogrammed_cpu.hpp"

#include <iomanip>
#include <iostream>

#include "isa_config.hpp"

namespace mc {
namespace {

const char* busDriverName(BusDriver d) {
    switch (d) {
        case BusDriver::None: return "none";
        case BusDriver::Pc: return "PC";
        case BusDriver::PcPlus1: return "PC+1";
        case BusDriver::PcNext: return "PC_NEXT";
        case BusDriver::PcPlusImm: return "PC+imm";
        case BusDriver::PcNextPlusImm: return "PC_NEXT+imm";
        case BusDriver::RegA: return "A";
        case BusDriver::RegB: return "B";
        case BusDriver::Imm: return "IMM";
        case BusDriver::AluOut: return "ALUOut";
        case BusDriver::FpuOut: return "FPUOut";
        case BusDriver::Mdr: return "MDR";
        case BusDriver::ReadData: return "READ_DATA";
        case BusDriver::ExResult: return "EX_RESULT";
        case BusDriver::CsrOut: return "CSR";
        case BusDriver::Zero: return "zero";
        case BusDriver::Const1: return "const1";
        case BusDriver::Const2: return "const2";
        case BusDriver::ConstRA: return "ra";
        case BusDriver::IrRd: return "IR[rd]";
        case BusDriver::IrRs1: return "IR[rs1]";
        case BusDriver::IrRs2: return "IR[rs2]";
        case BusDriver::IrImmSigned16: return "IR[imm16]";
        case BusDriver::IrImmBranch: return "IR[offset]";
        case BusDriver::IrImmMovu: return "IR[imm16]";
        case BusDriver::IrImmMovh: return "IR[imm16]<<16";
        case BusDriver::IrImmCsr: return "IR[csr]";
        case BusDriver::IrImmFmov: return "IR[imm21]";
        case BusDriver::RfA: return "RF[rs1]";
        case BusDriver::RfB: return "RF[rs2]";
        case BusDriver::ImemOut: return "IMEM";
    }
    return "?";
}

} // namespace

MicroprogrammedCPU::MicroprogrammedCPU(bool four_stage, bool trace)
    : four_stage_(four_stage),
      trace_(trace),
      fetch_(store_, imem_),
      decode_(store_, rf_),
      execute_(store_, csu_),
      ma1_(store_, dmem_),
      ma2_(store_, dmem_),
      wb_(store_, rf_),
      merged_(store_, dmem_, rf_) {
    reset();
}

void MicroprogrammedCPU::loadProgram(const std::vector<std::string>& binary) {
    imem_.load(binary);
    reset();
}

void MicroprogrammedCPU::reset() {
    rf_.reset();
    csu_.reset();
    dmem_.reset();

    fetch_.reset();
    decode_.reset();
    execute_.reset();
    ma1_.reset();
    ma2_.reset();
    wb_.reset();
    merged_.reset();

    if_id_.reset();
    id_ex_.reset();
    ex_mem_.reset();
    ma1_ma2_.reset();
    ma2_wb_.reset();

    pending_status_ = StatusInputs{};
    halted_ = false;
    wfi_ = false;
    total_cycles_ = 0;
    stall_cycles_ = 0;
    retired_ = 0;
}

bool MicroprogrammedCPU::step() {
    if (halted_)
        return false;

    // ---- 0. control/status unit: flags, traps, wfi, eret ----
    StatusSignals ss = csu_.clockNext(pending_status_);
    pending_status_ = StatusInputs{};

    // Snapshot the instructions still waiting in MA1/MA2.  The stages below
    // shift these latches, and the interlock must see the pre-shift producers.
    const LatchMem hz_ma1 = ex_mem_;
    const LatchMem hz_ma2 = ma1_ma2_;

    // ---- 1. downstream stages process the previous cycle's results ----
    if (four_stage_) {
        const bool old_valid = ex_mem_.valid;
        const bool old_ebreak = ex_mem_.is_ebreak;
        bool hit = true;
        int stall = 0;
        merged_.clock(ex_mem_, hit, stall);
        if (stall > 0) {
            stall_cycles_ += static_cast<std::uint64_t>(stall);
            total_cycles_ += static_cast<std::uint64_t>(stall);
        }
        if (old_valid) {
            ++retired_;
            if (old_ebreak) {
                halted_ = true;
                return false;
            }
        }
    } else {
        const bool wb_valid = ma2_wb_.valid;
        const bool wb_ebreak = ma2_wb_.is_ebreak;
        wb_.clock(ma2_wb_);
        if (wb_valid) {
            ++retired_;
            if (wb_ebreak) {
                halted_ = true;
                return false;
            }
        }

        int stall = 0;
        bool hit = true;
        ma2_.clock(ma1_ma2_, ma2_wb_, stall);
        if (stall > 0) {
            stall_cycles_ += static_cast<std::uint64_t>(stall);
            total_cycles_ += static_cast<std::uint64_t>(stall);
        }
        ma1_.clock(ex_mem_, ma1_ma2_, hit);
    }

    // ---- 2. EX ----
    ExSignals ex = execute_.clock(id_ex_, ex_mem_);
    pending_status_ = ex.status;
    pending_status_.is_ebreak = false;   // ebreak halts at WB instead of trapping

    // ---- 3. redirect priority: trap > eret > branch ----
    bool redirect = false;
    std::uint32_t target = 0;
    if (ss.is_trap) {
        redirect = true;
        target = ss.trap_vector;
    } else if (ss.is_eret) {
        redirect = true;
        target = ss.epc;
    } else if (ex.branch_taken) {
        redirect = true;
        target = ex.branch_target;
    }
    if (ss.halted)
        wfi_ = true;

    // ---- 4. hazard interlock / freeze ----
    const bool ex_busy = execute_.isBusy();
    bool raw_stall = false;
    if (!redirect && if_id_.valid) {
        DecodeInfo consumer = decode_.peek(if_id_);
        if (consumer.valid)
            raw_stall = producerConflict(consumer, hz_ma1, hz_ma2);
    }
    const bool front_stall = ex_busy || raw_stall || wfi_;

    // ---- 5. ID ----
    if (!ex_busy) {
        decode_.clock(if_id_, id_ex_, front_stall, redirect);
        if (decode_.illegal()) {
            csu_.raiseTrap(ExceptionCause::IllegalInstruction,
                           if_id_.valid ? if_id_.pc : fetch_.pc());
            decode_.clearIllegal();
        }
    } else if (redirect) {
        id_ex_.reset();
    }

    // ---- 6. IF ----
    FetchOut fo = fetch_.clock(front_stall, redirect, redirect, target);
    if (!fo.hold)
        if_id_ = fo.latch;

    // ---- 7. stats / trace ----
    ++total_cycles_;
    if (front_stall)
        ++stall_cycles_;

    if (trace_)
        printCycleHeader();

    // ---- 8. termination ----
    const bool downstream_empty =
        !ex_mem_.valid &&
        (four_stage_ || (!ma1_ma2_.valid && !ma2_wb_.valid));

    if (!if_id_.valid && !id_ex_.valid && downstream_empty &&
        !execute_.isBusy() && fetch_.pc() >= imem_.size()) {
        halted_ = true;
        return false;
    }

    // wfi: stop once the downstream pipeline has drained; IF/ID may still hold
    // the speculative instruction after wfi, which is fine.
    if (wfi_ && !id_ex_.valid && downstream_empty && !execute_.isBusy()) {
        halted_ = true;
        return false;
    }

    return true;
}

void MicroprogrammedCPU::run(std::uint64_t max_cycles) {
    while (!halted_ && total_cycles_ < max_cycles) {
        if (!step())
            break;
    }
}

bool MicroprogrammedCPU::writesMatch(const LatchMem& producer,
                                     const DecodeInfo& consumer) const {
    if (!producer.valid || !producer.is_wb)
        return false;
    if (consumer.reads_a && consumer.src_a != 12 && consumer.src_a == producer.rd)
        return true;
    if (consumer.reads_b && consumer.src_b != 12 && consumer.src_b == producer.rd)
        return true;
    return false;
}

bool MicroprogrammedCPU::writesMatchIDEX(const DecodeInfo& consumer) const {
    if (!id_ex_.valid)
        return false;
    if (!id_ex_.control.test(static_cast<std::size_t>(Signal::IS_WB)))
        return false;
    if (consumer.reads_a && consumer.src_a != 12 && consumer.src_a == id_ex_.rd)
        return true;
    if (consumer.reads_b && consumer.src_b != 12 && consumer.src_b == id_ex_.rd)
        return true;
    return false;
}

bool MicroprogrammedCPU::producerConflict(const DecodeInfo& consumer,
                                          const LatchMem& ma1,
                                          const LatchMem& ma2) const {
    if (writesMatchIDEX(consumer))
        return true;

    // 4-stage: MEM/WB runs before ID, so its register write is visible to the
    // instruction being decoded.  6-stage: MA1 and MA2 have not written the RF
    // yet, so they must interlock.  WB is excluded because it runs before ID.
    if (four_stage_)
        return false;

    if (writesMatch(ma1, consumer))
        return true;
    if (writesMatch(ma2, consumer))
        return true;
    return false;
}

bool MicroprogrammedCPU::readMemory(std::uint32_t address,
                                    std::uint32_t& value) const {
    value = dmem_.read(address);
    return true;
}

void MicroprogrammedCPU::dumpRegisters() const {
    const auto& regs = rf_.getAll();
    for (int i = 0; i < 16; ++i) {
        std::cout << "r" << std::setw(2) << std::setfill(' ') << i << " : 0x"
                  << std::hex << std::setw(8) << std::setfill('0') << regs[i]
                  << std::dec;
        if (i % 4 == 3) std::cout << '\n';
        else            std::cout << "  ";
    }
}

void MicroprogrammedCPU::dumpPipeline() const {
    auto dumpMem = [](const char* name, const LatchMem& l) {
        std::cout << "  " << name << " valid=" << l.valid
                  << " key=" << MicroprogramStore::keyString(l.micro_key)
                  << " res=0x" << std::hex << l.result << std::dec
                  << " rd=r" << static_cast<int>(l.rd)
                  << " ld=" << l.is_ld << " st=" << l.is_st
                  << " wb=" << l.is_wb << " br=" << l.branch_taken << "\n";
    };

    std::cout << "  IF/ID valid=" << if_id_.valid
              << " pc=" << if_id_.pc
              << " ir=0x" << std::hex << if_id_.ir << std::dec << "\n";
    std::cout << "  ID/EX valid=" << id_ex_.valid
              << " key=" << MicroprogramStore::keyString(id_ex_.micro_key)
              << " rd=r" << static_cast<int>(id_ex_.rd)
              << " A=0x" << std::hex << id_ex_.a
              << " B=0x" << id_ex_.b
              << " IMM=0x" << id_ex_.imm << std::dec << "\n";
    dumpMem("EX/MEM", ex_mem_);
    if (!four_stage_) {
        dumpMem("MA1/MA2", ma1_ma2_);
        dumpMem("MA2/WB", ma2_wb_);
    }
}

void MicroprogrammedCPU::dumpMicro() const {
    auto dump = [](const char* name, const MicrocontrolExecutionUnit& unit) {
        std::cout << "  " << std::setw(12) << std::left << name << std::right
                  << " uPC=" << std::setw(2) << static_cast<int>(unit.microPC())
                  << "  MIR=" << std::setw(24) << std::left
                  << (unit.mir().label ? unit.mir().label : "") << std::right
                  << " bus=" << std::setw(10) << std::left
                  << busDriverName(unit.bus().driver()) << std::right
                  << " value=0x" << std::hex << std::setw(8)
                  << std::setfill('0') << unit.bus().read()
                  << std::dec << std::setfill(' ')
                  << " ops=" << unit.lastTrace().size() << "\n";
    };

    std::cout << "Microcontrol execution units:\n";
    dump("IF", fetch_);
    dump("ID", decode_);
    dump("EX", execute_);
    if (!four_stage_) {
        dump("MA1", ma1_);
        dump("MA2", ma2_);
        dump("WB", wb_);
    } else {
        dump("MEM/WB", merged_);
    }
}

void MicroprogrammedCPU::dumpTrace() const {
    auto dump = [](const char* name,
                   const std::vector<Microinstruction>& trace) {
        std::cout << "  " << name << ":";
        if (trace.empty()) {
            std::cout << " (bubble)\n";
            return;
        }
        for (const auto& mi : trace)
            std::cout << " [" << (mi.label ? mi.label : "?") << "]";
        std::cout << "\n";
    };

    std::cout << "Last-clock micro-op traces:\n";
    dump("IF", fetch_.lastTrace());
    dump("ID", decode_.lastTrace());
    dump("EX", execute_.lastTrace());
    if (!four_stage_) {
        dump("MA1", ma1_.lastTrace());
        dump("MA2", ma2_.lastTrace());
        dump("WB", wb_.lastTrace());
    } else {
        dump("MEM/WB", merged_.lastTrace());
    }
}

void MicroprogrammedCPU::printCycleHeader() const {
    std::cout << "[cyc " << std::setw(4) << total_cycles_ << "]"
              << " PC=" << std::setw(3) << fetch_.pc()
              << " IF/ID=" << (if_id_.valid ? "V" : "-")
              << " ID/EX=" << (id_ex_.valid ? "V" : "-")
              << " EX/MEM=" << (ex_mem_.valid ? "V" : "-")
              << " EX.uPC=" << static_cast<int>(execute_.microPC())
              << " (" << (execute_.mir().label ? execute_.mir().label : "")
              << ")\n";
}

} // namespace mc
