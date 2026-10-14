// six_stage_memory.hpp
// MA1, MA2 and WB stages of the 6 stage pipeline.

#ifndef SIX_STAGE_MEMORY_HPP
#define SIX_STAGE_MEMORY_HPP

#include "six_stage_latches.hpp"
#include "memory_system.hpp"
#include "writeback_common.hpp"

// MA1: address goes into MAR, L1 tags are checked and the hit signal is made.
class MA1Stage
{
public:
    // m1_hit is the M1_Hit signal. It goes to the hazard unit and it must also be carried
    // into MA2 together with the returned latch (the latch has no field for it).
    // miss = (is_Ld || is_St) && !m1_hit
    MA1_MA2_Latch run(const EX_MA1_Latch &in, bool &m1_hit)
    {
        MA1_MA2_Latch out;   // starts as a bubble (is_valid = false)
        m1_hit = true;

        if (!in.is_valid)
            return out;

        // just pass everything forward
        out.exResult = in.exResult;
        out.st_data = in.st_data;
        out.is_Ld = in.is_Ld;
        out.is_St = in.is_St;
        out.is_Wb = in.is_Wb;
        out.is_Clear = in.is_Clear;
        out.rd = in.rd;
        out.is_valid = true;
        out.is_Ecall = in.is_Ecall;
        out.is_Ebreak = in.is_Ebreak;
        out.is_Eret = in.is_Eret;
        out.is_Wfi = in.is_Wfi;
        out.stack_fault = false;   // stack check is not part of the memory system

        bool is_mem_op = in.is_Ld || in.is_St;
        m1_hit = memProbe(in.exResult, is_mem_op);
        return out;
    }
};

// MA2: L1 data array is accessed and the MDR is filled (lower memory is used on a miss).
class MA2Stage
{
public:
    // stall_cycles = extra cycles needed because of a miss (0 on a hit)
    MA2_WB_Latch run(const MA1_MA2_Latch &in, bool m1_hit, int &stall_cycles)
    {
        MA2_WB_Latch out;   // starts as a bubble
        stall_cycles = 0;

        if (!in.is_valid)
            return out;

        out.exResult = in.exResult;
        out.is_Ld = in.is_Ld;
        out.is_Wb = in.is_Wb;
        out.is_Clear = in.is_Clear;
        out.rd = in.rd;
        out.is_valid = true;

        // only loads and stores go to the cache, other instructions just pass through
        if (in.is_Ld || in.is_St)
            out.read_data = memAccess(in.exResult, in.is_Ld, in.is_St, in.st_data, m1_hit, stall_cycles);

        return out;
    }
};

// WB: writes the result into the register file
class WBStage
{
    RegisterFile *rf;

public:
    WBStage(RegisterFile *r)
    {
        rf = r;
    }

    void run(const MA2_WB_Latch &in)
    {
        writeBackToRegFile(*rf, in.is_valid, in.is_Wb, in.is_Clear, in.is_Ld, in.rd, in.exResult, in.read_data);
    }
};

#endif