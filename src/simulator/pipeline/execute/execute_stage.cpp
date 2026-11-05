// execute_stage.cpp
// Execute (EX) stage implementation for the RISC201 pipeline.
//
// Compile with -DFOUR_STAGE_PIPELINE for the 4-stage variant;
// leave the macro undefined (or absent) for the 6-stage variant.

#include "execute_stage.hpp"
#include "alu.hpp"
#include "fpu.hpp"
#include "control_signals.hpp"
#include "control_status_unit.hpp"

#include <cstdint>

// ---- small helpers -------------------------------------------------------

static inline bool sig(const ControlSignals& c, Signal s) {
    return c[s];
}

// Determine whether this instruction is a plain fmov:
// IS_FLOAT is set, IS_WB is set, but none of FADD/FSUB/FMUL/FDIV/FCMP.
static inline bool isFmov(const ControlSignals& c) {
    return  sig(c, Signal::IS_FLOAT)  &&
           !sig(c, Signal::IS_FADD)   &&
           !sig(c, Signal::IS_FSUB)   &&
           !sig(c, Signal::IS_FMUL)   &&
           !sig(c, Signal::IS_FDIV)   &&
           !sig(c, Signal::IS_FCMP);
}

// Copy the common fields that every instruction needs in the output latch.
static inline void initOut(ExOutLatch& out, const ID_EX_Latch& in) {
    out.reset();
    out.is_valid  = in.is_valid;
    out.rd        = in.rd;
    out.pc_next   = in.pc_next;
    out.is_Wb     = sig(in.control, Signal::IS_WB);
    out.is_Ld     = sig(in.control, Signal::IS_LD);
    out.is_St     = sig(in.control, Signal::IS_ST);
    out.is_Clear  = sig(in.control, Signal::IS_CLEAR);
    out.is_Call   = sig(in.control, Signal::IS_CALL);
    out.is_Ecall  = sig(in.control, Signal::IS_SYSCALL);
    out.is_Ebreak = sig(in.control, Signal::IS_EBREAK);
    out.is_Eret   = sig(in.control, Signal::IS_ERET);
    out.is_Wfi    = sig(in.control, Signal::IS_WFI);
}

// -------------------------------------------------------------------------

ExOut ExecuteStage::clockNext(const ID_EX_Latch& in, ExOutLatch& out) {
    ExOut result;

    if (!in.is_valid) {
        out.reset();
        return result;
    }

    initOut(out, in);

    const ControlSignals& ctrl = in.control;

    // Fill the StatusInputs PC (instruction index of this EX instruction).
    // pc_next is pc+1, so pc = pc_next - 1.
    result.status_in.pc = (in.pc_next > 0) ? in.pc_next - 1u : 0u;

    // ------------------------------------------------------------------ //
    //  TRAPS: ecall / ebreak / eret / wfi — no ALU work needed.          //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_SYSCALL) || sig(ctrl, Signal::IS_EBREAK) ||
        sig(ctrl, Signal::IS_ERET)    || sig(ctrl, Signal::IS_WFI)) {
        result.status_in.is_ecall  = sig(ctrl, Signal::IS_SYSCALL);
        result.status_in.is_ebreak = sig(ctrl, Signal::IS_EBREAK);
        result.status_in.is_eret   = sig(ctrl, Signal::IS_ERET);
        result.status_in.is_wfi    = sig(ctrl, Signal::IS_WFI);
        return result;
    }

    // ------------------------------------------------------------------ //
    //  FLOATING-POINT                                                     //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_FLOAT)) {
        if (isFmov(in.control)) {
            // fmov: the 21-bit immediate was stored in immx by decode.
            out.exResult = in.immx;
        } else {
            FPU::FPUOp fop = FPU::OP_FADD;
            if      (sig(ctrl, Signal::IS_FADD)) fop = FPU::OP_FADD;
            else if (sig(ctrl, Signal::IS_FSUB)) fop = FPU::OP_FSUB;
            else if (sig(ctrl, Signal::IS_FMUL)) fop = FPU::OP_FMUL;
            else if (sig(ctrl, Signal::IS_FDIV)) fop = FPU::OP_FDIV;
            else if (sig(ctrl, Signal::IS_FCMP)) fop = FPU::OP_FCMP;

            FPUResult fr = fpu_.execute(in.op1, in.op2, fop);
            out.exResult = fr.res;

            if (sig(ctrl, Signal::IS_FCMP)) {
                // fcmp sets flags: equal = both zero or same bits; greater = !Z && !N
                result.status_in.flag_write   = true;
                result.status_in.flag_equal   = fr.Z;
                result.status_in.flag_greater = !fr.Z && !fr.N;
            }
        }
        return result;
    }

    // ------------------------------------------------------------------ //
    //  UNCONDITIONAL BRANCH  (b, call, ret)                              //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_UBRANCH)) {
        if (sig(ctrl, Signal::IS_RET)) {
            // ret: target is in op1 (r15 / ra), read by decode
            result.branch_taken  = true;
            result.branch_target = in.op1;
            out.exResult         = in.op1;
        } else if (sig(ctrl, Signal::IS_CALL)) {
            // call: link = pc_next, target = pc + immx
            uint32_t target      = in.pc_next - 1u + in.immx;
            result.branch_taken  = true;
            result.branch_target = target;
            out.exResult         = in.pc_next;  // return address stored to r15 via WB
        } else {
            // unconditional branch (b)
            uint32_t target      = in.pc_next - 1u + in.immx;
            result.branch_taken  = true;
            result.branch_target = target;
            out.is_Wb            = false; // branches don't write registers
        }
        return result;
    }

    // ------------------------------------------------------------------ //
    //  CONDITIONAL BRANCHES  (beq, bne, bgt, blt)                       //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_BEQ) || sig(ctrl, Signal::IS_BNE) ||
        sig(ctrl, Signal::IS_BGT) || sig(ctrl, Signal::IS_BLT)) {
        bool taken = false;
        bool eq    = csu_.flagEqual();
        bool gt    = csu_.flagGreater();
        if      (sig(ctrl, Signal::IS_BEQ)) taken = eq;
        else if (sig(ctrl, Signal::IS_BNE)) taken = !eq;
        else if (sig(ctrl, Signal::IS_BGT)) taken = gt;
        else if (sig(ctrl, Signal::IS_BLT)) taken = !gt && !eq;

        if (taken) {
            result.branch_taken  = true;
            result.branch_target = in.pc_next - 1u + in.immx;
        }
        out.is_Wb = false; // branches don't write registers
        return result;
    }

    // ------------------------------------------------------------------ //
    //  CSR ACCESS                                                         //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_CSRR)) {
        uint32_t val = 0;
        csu_.readCsr(in.immx, val);
        out.exResult = val;
        return result;
    }
    if (sig(ctrl, Signal::IS_CSRW)) {
        csu_.writeCsr(in.immx, in.op1);
        out.is_Wb = false;
        return result;
    }

    // ------------------------------------------------------------------ //
    //  MOVU / MOVH — immediate already correctly formed by decode         //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_MOVU) || sig(ctrl, Signal::IS_MOVH)) {
        out.exResult = in.immx;
        return result;
    }

    // ------------------------------------------------------------------ //
    //  CLEAR (clrz) — write 0 to rd                                      //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_CLEAR)) {
        out.exResult = 0;
        return result;
    }

    // ------------------------------------------------------------------ //
    //  CHK — stack bounds check: if op1 >= op2 raise a stack fault       //
    // ------------------------------------------------------------------ //
    if (sig(ctrl, Signal::IS_CHK)) {
        // Treat both as unsigned for the bounds comparison.
        if (in.op1 >= in.op2) {
            out.stack_fault = true;
            result.status_in.chk_fault = true;
        }
        out.is_Wb = false;
        return result;
    }

    // ------------------------------------------------------------------ //
    //  INTEGER ALU                                                        //
    // ------------------------------------------------------------------ //
    {
        // For memory ops (ld/st), the second ALU input is always immx.
        // For normal ALU ops, immx is used when IS_IMMEDIATE is set.
        uint32_t operand2 = (sig(ctrl, Signal::IS_LD)  || sig(ctrl, Signal::IS_ST) ||
                             sig(ctrl, Signal::IS_IMMEDIATE))
                            ? in.immx : in.op2;

        ALU::ALUOp op = ALU::OP_ADD;
        if      (sig(ctrl, Signal::IS_ADD)) op = ALU::OP_ADD;
        else if (sig(ctrl, Signal::IS_SUB)) op = ALU::OP_SUB;
        else if (sig(ctrl, Signal::IS_MUL)) op = ALU::OP_MUL;
        else if (sig(ctrl, Signal::IS_DIV)) op = ALU::OP_DIV;
        else if (sig(ctrl, Signal::IS_MOD)) op = ALU::OP_MOD;
        else if (sig(ctrl, Signal::IS_CMP)) op = ALU::OP_CMP;
        else if (sig(ctrl, Signal::IS_AND)) op = ALU::OP_AND;
        else if (sig(ctrl, Signal::IS_OR))  op = ALU::OP_OR;
        else if (sig(ctrl, Signal::IS_XOR)) op = ALU::OP_XOR;
        else if (sig(ctrl, Signal::IS_NOT)) op = ALU::OP_NOT;
        else if (sig(ctrl, Signal::IS_LSL)) op = ALU::OP_LSL;
        else if (sig(ctrl, Signal::IS_LSR)) op = ALU::OP_LSR;
        else if (sig(ctrl, Signal::IS_ASR)) op = ALU::OP_ASR;
        // ld/st: address = op1 + immx via ADD (default)

        // For NOT, the hardware only uses operand2 (rs2 or immediate).
        uint32_t op1_val = sig(ctrl, Signal::IS_NOT) ? 0u : in.op1;

        ALUResult ar = alu_.execute(op1_val, operand2, op);
        out.exResult = ar.res;

        // For stores, the data to store is in op2 (the rd field read by decode).
        if (sig(ctrl, Signal::IS_ST)) {
            out.st_data = in.op2;
        }

        // CMP updates flags, not a register.
        if (sig(ctrl, Signal::IS_CMP)) {
            result.status_in.flag_write   = true;
            result.status_in.flag_equal   = ar.Z;
            result.status_in.flag_greater = !ar.Z && !ar.N;
            out.is_Wb = false;
        }
    }

    return result;
}
