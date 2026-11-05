#include "instruction_decode.hpp"
#include "microcontrol_unit.hpp"

#include <bitset>
#include <string>

namespace {

// ---- Instruction field layout (32-bit word, MSB = bit 31) -----------------
//   [31:25] opcode (6-bit opcode + 1 format bit)  -> used by the MCU
//   [24:21] rd
//   [20:17] rs1
//   [16:13] rs2                       (R-format)
//   [16:0]  immediate (17 bits)       (I-format)
//   [24:0]  branch / call offset (25 bits)
// CHANGE THESE if your assembler encodes fields differently.
constexpr unsigned kRdShift  = 21;
constexpr unsigned kRs1Shift = 17;
constexpr unsigned kRs2Shift = 13;
constexpr unsigned kImmBits    = 17;
constexpr unsigned kBranchBits = 25;

// Assembler emits branch offsets in bytes (divisible by 4)? Then convert to
// instruction-index units here, because the PC steps by 1.
constexpr bool kBranchOffsetInBytes = true;

constexpr uint8_t kRA = 15;   // link register used by call / ret

inline bool has(const ControlWord& cw, Signal s) {
    return cw.test(static_cast<std::size_t>(s));
}

inline uint32_t maskBits(unsigned bits) {
    return (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
}

inline uint32_t signExtend(uint32_t v, unsigned bits) {
    v &= maskBits(bits);
    const uint32_t m = 1u << (bits - 1);
    return (v ^ m) - m;
}

inline bool isPcRelative(const ControlWord& cw) {
    // b, beq, bne, bgt, blt, call (ret also sets UBRANCH but uses a register)
    if (has(cw, Signal::IS_RET)) return false;
    return has(cw, Signal::IS_UBRANCH) || has(cw, Signal::IS_BEQ) ||
           has(cw, Signal::IS_BNE)     || has(cw, Signal::IS_BGT) ||
           has(cw, Signal::IS_BLT)     || has(cw, Signal::IS_CALL);
}

uint32_t buildImmediate(uint32_t ir, const ControlWord& cw) {
    if (!has(cw, Signal::IS_IMMEDIATE)) return 0;

    if (isPcRelative(cw)) {
        uint32_t off = signExtend(ir, kBranchBits);
        if (kBranchOffsetInBytes) {
            off = static_cast<uint32_t>(static_cast<int32_t>(off) >> 2);
        }
        return off;
    }
    if (has(cw, Signal::IS_MOVH)) return (ir & 0xFFFFu) << 16;   // upper half
    if (has(cw, Signal::IS_MOVU)) return ir & 0xFFFFu;           // zero-extend
    if (has(cw, Signal::IS_CSRR) || has(cw, Signal::IS_CSRW)) {
        return ir & 0xFFFFu;                                     // CSR index
    }
    return signExtend(ir, kImmBits);                             // default
}

} // namespace

// ---------------------------------------------------------------------------

OperandSources OperandFetchStage::sourcesOf(uint32_t ir, const ControlWord& cw) {
    OperandSources s;
    if (cw.none()) return s;                 // nop: no sources
    const uint8_t rd  = (ir >> kRdShift)  & 0xF;
    const uint8_t rs1 = (ir >> kRs1Shift) & 0xF;
    const uint8_t rs2 = (ir >> kRs2Shift) & 0xF;
    const bool imm = has(cw, Signal::IS_IMMEDIATE);

    // Instructions that read system registers.
    if (has(cw, Signal::IS_SYSCALL)) {
        // Syscall id in r1, argument in r2 (host-emulation convention).
        s.uses_a = true; s.a = 1;
        s.uses_b = true; s.b = 2;
        return s;
    }
    if (has(cw, Signal::IS_RET)) {
        s.uses_a = true; s.a = kRA;
        return s;
    }
    if (has(cw, Signal::IS_ST)) {
        s.uses_a = true; s.a = rs1;     // base address
        s.uses_b = true; s.b = rd;      // value to store
        return s;
    }
    if (has(cw, Signal::IS_LD)) {
        s.uses_a = true; s.a = rs1;
        return s;
    }
    if (has(cw, Signal::IS_CSRW)) {          // csrw reads one register
    s.uses_a = true; s.a = rs1;
    return s;
    }
    if (has(cw, Signal::IS_CHK)) {           // chk reads two registers (R_TYPE_2)
    s.uses_a = true; s.a = rs1;
    s.uses_b = true; s.b = rs2;
    return s;
    }

    // No register sources.
    if (has(cw, Signal::IS_MOVU) || has(cw, Signal::IS_MOVH) ||
        has(cw, Signal::IS_CLEAR) || has(cw, Signal::IS_CSRR) ||
        has(cw, Signal::IS_WFI)   || has(cw, Signal::IS_ERET) ||
        has(cw, Signal::IS_EBREAK) || isPcRelative(cw)) {
        return s;
    }

    // not: single operand (rs2, or immediate).
    if (has(cw, Signal::IS_NOT)) {
        if (!imm) { s.uses_b = true; s.b = rs2; }
        return s;
    }

    // Integer ALU, cmp and FPU: rs1 and (rs2 or immediate).
    s.uses_a = true; s.a = rs1;
    if (!imm) { s.uses_b = true; s.b = rs2; }
    return s;
}

OperandSources OperandFetchStage::peekSources(const IF_ID_Latch& in,
                                              const MicroprogrammedControlUnit& mcu) {
    OperandSources none;
    if (!in.is_valid) return none;

    ControlWord cw;
    if (!mcu.decodeInstruction(std::bitset<32>(in.ir).to_string(), cw)) return none;
    return sourcesOf(in.ir, cw);
}

uint32_t OperandFetchStage::branchTarget(uint32_t pc_next, uint32_t immx) {
    return pc_next - 1u + immx;      // unsigned wrap-around handles negatives
}

void OperandFetchStage::clockNext(const IF_ID_Latch& in,
                                  ID_EX_Latch& out,
                                  const RegArray& regs,
                                  const MicroprogrammedControlUnit& mcu,
                                  bool is_stall,
                                  bool is_flush) {
    // 1. Flush wins over everything.
    if (is_flush) {
        out.flush();
        return;
    }

    // 2. Stall: bubble goes down, IF/ID is held by the IF stage.
    if (is_stall) {
        out.reset();
        return;
    }

    // 3. Nothing valid to decode.
    if (!in.is_valid) {
        out.reset();
        return;
    }

    // 4. Decode through the microprogrammed control unit.
    ControlWord cw;
    if (!mcu.decodeInstruction(std::bitset<32>(in.ir).to_string(), cw)) {
        illegal_    = true;
        illegal_pc_ = in.pc;
        illegal_ir_ = in.ir;
        out.reset();
        return;
    }

    // 5. Read operands and build the ID/EX latch.
    const OperandSources src = sourcesOf(in.ir, cw);

    out.reset();
    out.control.bits = cw;
    out.pc_next      = in.pc_next;
    out.is_valid     = true;

    // call writes its link address into ra; everything else uses the rd field.
    out.rd  = has(cw, Signal::IS_CALL) ? kRA
                                       : static_cast<uint8_t>((in.ir >> kRdShift) & 0xF);
    out.op1 = src.uses_a ? regs[src.a & 0xF] : 0;
    out.op2 = src.uses_b ? regs[src.b & 0xF] : 0;
    out.immx = buildImmediate(in.ir, cw);
}