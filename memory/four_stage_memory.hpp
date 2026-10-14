// four_stage_memory.hpp
// Memory access and write back combined in one stage (4 stage pipeline).
// It uses the same cache parts as the 6 stage design, just everything happens in one stage.

#ifndef FOUR_STAGE_MEMORY_HPP
#define FOUR_STAGE_MEMORY_HPP

#include "four_stage_latches.hpp"
#include "memory_system.hpp"
#include "writeback_common.hpp"

class MemoryWritebackStage
{
    RegisterFile *rf;

public:
    MemoryWritebackStage(RegisterFile *r)
    {
        rf = r;
    }

    // This is the last stage so there is no output latch, the result goes straight to the register file.
    // m1_hit       : hit signal for the hazard unit (miss = (is_Ld || is_St) && !m1_hit)
    // stall_cycles : how many cycles this stage has to be held (0 on a hit)
    void run(const EX_MA_Latch &in, bool &m1_hit, int &stall_cycles)
    {
        m1_hit = true;
        stall_cycles = 0;

        if (!in.is_valid)
            return;

        bool is_mem_op = in.is_Ld || in.is_St;
        uint32_t read_data = 0;

        // first part (like MA1): MAR and tag check
        m1_hit = memProbe(in.exResult, is_mem_op);

        // second part (like MA2): data array / lower memory
        if (is_mem_op)
            read_data = memAccess(in.exResult, in.is_Ld, in.is_St, in.st_data, m1_hit, stall_cycles);

        // last part: write back
        writeBackToRegFile(*rf, true, in.is_Wb, in.is_Clear, in.is_Ld, in.rd, in.exResult, read_data);
    }
};

#endif