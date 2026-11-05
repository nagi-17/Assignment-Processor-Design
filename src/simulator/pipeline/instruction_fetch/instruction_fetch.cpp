#include "instruction_fetch.hpp"
#include <iostream>
#include <stdexcept>

void InstructionMemory::loadProgram(const std::vector<std::string>& programStrings) {
    memory = programStrings;
}

size_t InstructionMemory::size() const {
    return memory.size();
}

bool InstructionMemory::readInstruction(uint32_t idx, uint32_t& ir) const {
    if (idx >= memory.size()) return false;
    const std::string& s = memory[idx];
    if (s.size() < 32) return false;
    uint32_t v = 0;
    for (int i = 0; i < 32; ++i) {
        if (s[i] != '0' && s[i] != '1') return false;
        v = (v << 1) | (s[i] == '1');
    }
    ir = v;
    return true;
}

InstructionFetchStage::InstructionFetchStage(uint32_t start_pc) : pc(start_pc) {}

uint32_t InstructionFetchStage::getPC() const { 
    return pc; 
}

void InstructionFetchStage::setPC(uint32_t new_pc) { 
    pc = new_pc; 
}

void InstructionFetchStage::clockNext(
    const InstructionMemory& imem,
    IF_ID_Latch& out_latch,
    bool is_stall,
    bool is_flush,
    bool is_branch_taken,
    uint32_t branch_target,
    bool is_eret,
    uint32_t epc,
    bool is_trap,
    uint32_t trap_vector
){
    if (is_trap || is_eret || is_branch_taken) {
    out_latch.flush();
    pc = is_trap ? trap_vector : (is_eret ? epc : branch_target);
    return;
    }
    // 1. Handle Pipeline Flush
    if (is_flush) {
        out_latch.flush();
        return;
    }

    // 2. Handle Pipeline Stall / WFI Idle
    if (is_stall) {
        return;
    }

    // 3. Handle End-of-Program / Pipeline Drain
    if (pc >= imem.size()) {
        out_latch.reset();
        return;
    }

    uint32_t ir = 0;
    if (!imem.readInstruction(pc, ir)) {
        std::cerr << "[IMEM ERROR] bad instruction at index " << pc << "\n";
        out_latch.reset();
        return;                // let the main loop stop or raise a fault
    }
    out_latch.pc = pc;  out_latch.ir = ir;
    out_latch.pc_next = pc + 1;  out_latch.is_valid = true;
    pc = pc + 1;
}