#include "instruction_fetch.hpp"
#include <iostream>
#include <stdexcept>

void InstructionMemory::loadProgram(const std::vector<std::string>& programStrings) {
    memory = programStrings;
}

size_t InstructionMemory::size() const {
    return memory.size();
}

uint32_t InstructionMemory::readInstruction(uint32_t pc_index) const {
    if (pc_index < memory.size()) {
        const std::string& binStr = memory[pc_index];
        if (binStr.length() >= 32) {
            try {
                return static_cast<uint32_t>(std::stoul(binStr.substr(0, 32), nullptr, 2));
            } catch (const std::exception& e) {
                std::cerr << "[IMEM ERROR]: Malformed binary string at PC index " 
                          << pc_index << ": " << binStr << "\n";
                return 0;
            }
        }
    }
    return 0; // NOP fallback for out-of-bounds access
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
) {
    // 1. Handle Pipeline Flush
    if (is_flush) {
        out_latch.flush();
        if (is_trap) {
            pc = trap_vector;
        } else if (is_eret) {
            pc = epc;
        } else if (is_branch_taken) {
            pc = branch_target;
        }
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

    // 4. Perform Sequential Fetch & Latch Update
    uint32_t pc_next = pc + 1;
    uint32_t ir = imem.readInstruction(pc);

    out_latch.pc = pc;
    out_latch.ir = ir;
    out_latch.pc_next = pc_next;
    out_latch.is_valid = true;

    // 5. Next-PC Priority Multiplexer
    if (is_trap) {
        pc = trap_vector;
    } else if (is_eret) {
        pc = epc;
    } else if (is_branch_taken) {
        pc = branch_target;
    } else {
        pc = pc_next;
    }
}