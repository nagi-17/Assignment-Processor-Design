#include <iostream>
#include <cstdint>
#include <string>
#include <iomanip>

namespace RISC201
{
    constexpr uint8_t REG_ZERO = 12; // r12 is hardwired to 0
    constexpr uint8_t REG_RA = 15;   // r15 is return address register

    enum Opcode : uint8_t
    {
        // Integer ALU (00)
        OP_NOP = 0x00, // 000000
        OP_ADD = 0x01, // 000001
        OP_SUB = 0x02, // 000010
        OP_MUL = 0x03, // 000011
        OP_DIV = 0x04, // 000100
        OP_MOD = 0x05, // 000101
        OP_CMP = 0x06, // 000110
        OP_AND = 0x08, // 001000
        OP_OR = 0x09,  // 001001
        OP_XOR = 0x0A, // 001010
        OP_NOT = 0x0B, // 001011
        OP_LSL = 0x0C, // 001100
        OP_LSR = 0x0D, // 001101
        OP_ASR = 0x0E, // 001110

        // FPU inst. (01)
        OP_FADD = 0x11, // 010001
        OP_FSUB = 0x12, // 010010
        OP_FMUL = 0x13, // 010011
        OP_FDIV = 0x14, // 010100
        OP_FCMP = 0x16, // 010110
        OP_FMOV = 0x18, // 011000

        // Memory & System inst. (10)
        OP_LD = 0x20,   // 100000
        OP_ST = 0x21,   // 100001
        OP_MOVU = 0x22, // 100010
        OP_MOVH = 0x23, // 100011
        OP_CSRR = 0x28, // 101000
        OP_CSRW = 0x29, // 101001
        OP_CHK = 0x2C,  // 101100
        OP_CLRZ = 0x2D, // 101101

        // Control Flow inst. (11)
        OP_B = 0x30,      // 110000
        OP_BEQ = 0x31,    // 110001
        OP_BNE = 0x32,    // 110010
        OP_BGT = 0x33,    // 110011
        OP_BLT = 0x34,    // 110100
        OP_CALL = 0x38,   // 111000
        OP_RET = 0x39,    // 111001
        OP_ECALL = 0x3C,  // 111100
        OP_ERET = 0x3D,   // 111101
        OP_EBREAK = 0x3E, // 111110
        OP_WFI = 0x3F     // 111111
    };
}

// Decoded RISC201 Instruction Representation
struct Instruction
{
    uint32_t raw = 0;
    uint8_t opcode = RISC201::OP_NOP;
    bool is_imm = false;
    uint8_t rd = 0;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    uint32_t imm = 0;
    bool valid = false;

    static Instruction decode(uint32_t raw_word)
    {
        Instruction instr;
        instr.raw = raw_word;
        instr.opcode = (raw_word >> 26) & 0x3F;
        instr.is_imm = (raw_word >> 25) & 0x01;
        instr.rd = (raw_word >> 21) & 0x0F;
        instr.rs1 = (raw_word >> 17) & 0x0F;
        instr.rs2 = (raw_word >> 13) & 0x0F;
        instr.valid = (instr.opcode != RISC201::OP_NOP);

        if (instr.opcode == RISC201::OP_FMOV)
        {
            // F-Type immediate: 21 bits
            instr.imm = raw_word & 0x1FFFFF;
        }
        else if (instr.is_imm)
        {
            // I-Type immediate: 16 bits (bits 16:1)
            instr.imm = (raw_word >> 1) & 0xFFFF;
        }
        return instr;
    }

    static Instruction make_nop()
    {
        Instruction instr;
        instr.opcode = RISC201::OP_NOP;
        instr.valid = false;
        return instr;
    }

    // Identifies whether an instruction modifies a general-purpose register
    bool writes_register() const
    {
        if (!valid)
            return false;

        switch (opcode)
        {
        // Integer ALU writing rd
        case RISC201::OP_ADD:
        case RISC201::OP_SUB:
        case RISC201::OP_MUL:
        case RISC201::OP_DIV:
        case RISC201::OP_MOD:
        case RISC201::OP_AND:
        case RISC201::OP_OR:
        case RISC201::OP_XOR:
        case RISC201::OP_NOT:
        case RISC201::OP_LSL:
        case RISC201::OP_LSR:
        case RISC201::OP_ASR:
        // FPU writing rd
        case RISC201::OP_FADD:
        case RISC201::OP_FSUB:
        case RISC201::OP_FMUL:
        case RISC201::OP_FDIV:
        case RISC201::OP_FMOV:
        // Memory & System writing rd
        case RISC201::OP_LD:
        case RISC201::OP_MOVU: // movu is a pure producer
        case RISC201::OP_MOVH: // movh is a pure producer
        case RISC201::OP_CSRR:
        case RISC201::OP_CLRZ:
            // Writing to r12 (zero reg) does not alter architectural state
            return (rd != RISC201::REG_ZERO);

        // Subroutine Call writes r15 (ra)
        case RISC201::OP_CALL:
            return true;

        default:
            return false;
        }
    }

    uint8_t get_write_register() const
    {
        if (opcode == RISC201::OP_CALL)
        {
            return RISC201::REG_RA;
        }
        return rd;
    }

    // Source 1 dependency check
    bool reads_src1() const
    {
        if (!valid)
            return false;

        switch (opcode)
        {
        case RISC201::OP_ADD:
        case RISC201::OP_SUB:
        case RISC201::OP_MUL:
        case RISC201::OP_DIV:
        case RISC201::OP_MOD:
        case RISC201::OP_CMP:
        case RISC201::OP_AND:
        case RISC201::OP_OR:
        case RISC201::OP_XOR:
        case RISC201::OP_NOT:
        case RISC201::OP_LSL:
        case RISC201::OP_LSR:
        case RISC201::OP_ASR:
        case RISC201::OP_FADD:
        case RISC201::OP_FSUB:
        case RISC201::OP_FMUL:
        case RISC201::OP_FDIV:
        case RISC201::OP_FCMP:
        case RISC201::OP_LD:
        case RISC201::OP_ST:
        case RISC201::OP_CSRW:
        case RISC201::OP_CHK:
            return (rs1 != RISC201::REG_ZERO);

        case RISC201::OP_RET:
            // RET implicitly reads r15 (ra)
            return true;

        default:
            return false;
        }
    }

    uint8_t get_src1_register() const
    {
        if (opcode == RISC201::OP_RET)
        {
            return RISC201::REG_RA;
        }
        return rs1;
    }

    // Source 2 dependency check
    bool reads_src2() const
    {
        if (!valid)
            return false;

        // Store: rd supplies the payload data written to memory
        // (movu and movh are pure overwrites, so they do NOT read rd)
        if (opcode == RISC201::OP_ST)
        {
            return (rd != RISC201::REG_ZERO);
        }

        // FPU arithmetic instructions are strictly R-type
        if (opcode == RISC201::OP_FADD || opcode == RISC201::OP_FSUB ||
            opcode == RISC201::OP_FMUL || opcode == RISC201::OP_FDIV ||
            opcode == RISC201::OP_FCMP)
        {
            return (rs2 != RISC201::REG_ZERO);
        }

        // Bounds Check instruction reads rs2
        if (opcode == RISC201::OP_CHK)
        {
            return (rs2 != RISC201::REG_ZERO);
        }

        // Integer ALU instructions read rs2 only when format is R-type (I = 0)
        if (!is_imm)
        {
            switch (opcode)
            {
            case RISC201::OP_ADD:
            case RISC201::OP_SUB:
            case RISC201::OP_MUL:
            case RISC201::OP_DIV:
            case RISC201::OP_MOD:
            case RISC201::OP_CMP:
            case RISC201::OP_AND:
            case RISC201::OP_OR:
            case RISC201::OP_XOR:
            case RISC201::OP_LSL:
            case RISC201::OP_LSR:
            case RISC201::OP_ASR:
                return (rs2 != RISC201::REG_ZERO);
            default:
                return false;
            }
        }
        return false;
    }

    uint8_t get_src2_register() const
    {
        // Only ST uses the rd bitfield as an input operand source
        if (opcode == RISC201::OP_ST)
        {
            return rd;
        }
        return rs2;
    }
};

// Control Signals emitted by Hazard Detection Unit to Datapath
struct HazardControlSignals
{
    bool pc_stall = false;     // Freezes PC register
    bool if_of_stall = false;  // Freezes IF/OF pipeline register
    bool if_of_flush = false;  // Flushes IF/OF register to NOP
    bool of_ex_bubble = false; // Injects NOP into OF/EX register
    bool branch_taken = false; // Multiplexer select: redirect PC to target
    uint32_t branch_pc = 0;    // Target PC to load
};

// Hardware Hazard Detection Unit for 4-Stage RISC201
class HazardDetectionUnit
{
private:
    // Conflict detector 1: Checks RAW dependency between OF and EX_STAGE
    bool check_ex_conflict(const Instruction &OF_STAGE, const Instruction &EX_STAGE) const
    {
        if (!OF_STAGE.valid || !EX_STAGE.valid)
        {
            return false;
        }

        if (!EX_STAGE.writes_register())
        {
            return false;
        }

        uint8_t write_reg = EX_STAGE.get_write_register();

        if (OF_STAGE.reads_src1() && (OF_STAGE.get_src1_register() == write_reg))
        {
            return true;
        }

        if (OF_STAGE.reads_src2() && (OF_STAGE.get_src2_register() == write_reg))
        {
            return true;
        }

        return false;
    }

    // Conflict detector 2: Checks RAW dependency between OF and MWB_STAGE
    bool check_mwb_conflict(const Instruction &OF_STAGE, const Instruction &MWB_STAGE) const
    {
        if (!OF_STAGE.valid || !MWB_STAGE.valid)
        {
            return false;
        }

        if (!MWB_STAGE.writes_register())
        {
            return false;
        }

        uint8_t write_reg = MWB_STAGE.get_write_register();

        if (OF_STAGE.reads_src1() && (OF_STAGE.get_src1_register() == write_reg))
        {
            return true;
        }

        if (OF_STAGE.reads_src2() && (OF_STAGE.get_src2_register() == write_reg))
        {
            return true;
        }

        return false;
    }

public:
    HazardControlSignals evaluate(
        const Instruction &OF_STAGE,
        const Instruction &EX_STAGE,
        const Instruction &MWB_STAGE,
        bool branch_taken_ex,
        uint32_t branch_target_ex) const
    {
        HazardControlSignals ctrl;

        // 1. Branch-Lock Condition (Branch resolved inside EX_STAGE)
        if (branch_taken_ex)
        {
            ctrl.branch_taken = true;
            ctrl.branch_pc = branch_target_ex;

            // Allow PC to clock in the target address
            ctrl.pc_stall = false;

            // Flush speculative instructions in IF/OF and OF/EX registers
            ctrl.if_of_flush = true;
            ctrl.of_ex_bubble = true;
            ctrl.if_of_stall = false;
            return ctrl;
        }

        // 2. Data-Lock Condition (Evaluated across EX_STAGE and MWB_STAGE)
        bool conflict_ex = check_ex_conflict(OF_STAGE, EX_STAGE);
        bool conflict_mwb = check_mwb_conflict(OF_STAGE, MWB_STAGE);

        if (conflict_ex || conflict_mwb)
        {
            // Stall fetch & decode; bubble into execute
            ctrl.pc_stall = true;
            ctrl.if_of_stall = true;
            ctrl.if_of_flush = false;
            ctrl.of_ex_bubble = true;
        }
        else
        {
            // Normal execution flow
            ctrl.pc_stall = false;
            ctrl.if_of_stall = false;
            ctrl.if_of_flush = false;
            ctrl.of_ex_bubble = false;
        }

        return ctrl;
    }
};
