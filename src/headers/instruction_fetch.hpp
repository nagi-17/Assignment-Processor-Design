#ifndef INSTRUCTION_FETCH_HPP
#define INSTRUCTION_FETCH_HPP

#include <cstdint>
#include <vector>
#include <string>
#include "control_signals.hpp"
#ifdef FOUR_STAGE_PIPELINE
#include "four_stage_latches.hpp"
#else
#include "six_stage_latches.hpp"
#endif

class InstructionMemory {
    private:
        std::vector<std::string> memory;

    public:
        InstructionMemory() = default;

        void loadProgram(const std::vector<std::string>& programStrings);
        size_t size() const;
        bool readInstruction(uint32_t pc_index, uint32_t& ir) const;
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