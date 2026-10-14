#ifndef INSTRUCTION_FETCH_HPP
#define INSTRUCTION_FETCH_HPP

#include <cstdint>
#include <vector>
#include <string>
#include "control_signals.hpp"
#include "six_stage_latches.hpp" // or "four_stage_latches.hpp", as the IF/ID Latch class remains the same.

class InstructionMemory {
    private:
        std::vector<std::string> memory;

    public:
        InstructionMemory() = default;

        void loadProgram(const std::vector<std::string>& programStrings);
        size_t size() const;
        uint32_t readInstruction(uint32_t pc_index) const;
};

class InstructionFetchStage {
    private:
        uint32_t pc{0};

    public:
        InstructionFetchStage(uint32_t start_pc = 0);

        uint32_t getPC() const;
        void setPC(uint32_t new_pc);

        void clockNext(
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
        );
};

#endif