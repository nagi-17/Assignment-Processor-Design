#include "test_support.hpp"
#include "instruction_fetch.hpp"
#include "instruction_decode.hpp"
#include "microcontrol_unit.hpp"

#include <cstddef>

using namespace tf;

namespace {

using Regs = OperandFetchStage::RegArray;

// r[i] = 100 + i, so every register holds a distinct, recognisable value.
// op1 for rs1=3 must therefore be 103, and so on.
Regs makeRegs() {
    Regs r{};
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = static_cast<uint32_t>(100 + i);
    return r;
}

MicroprogrammedControlUnit& mcu() {
    static MicroprogrammedControlUnit m;
    return m;
}

// IF/ID latch at the given pc, holding a valid instruction.
IF_ID_Latch ifid(uint32_t ir, uint32_t pc = 5) {
    IF_ID_Latch l;
    l.pc = pc;
    l.ir = ir;
    l.pc_next = pc + 1;
    l.is_valid = true;
    return l;
}

bool sig(const ID_EX_Latch& o, Signal s) {
    return o.control.bits.test(static_cast<std::size_t>(s));
}

// ID/EX latch full of garbage, to prove the stage overwrites everything.
ID_EX_Latch dirtyOut() {
    ID_EX_Latch o;
    o.is_valid = true;
    o.rd = 9;
    o.op1 = 0xAAAA;
    o.op2 = 0xBBBB;
    o.immx = 0xCCCC;
    o.pc_next = 99;
    o.control.bits.set();
    return o;
}

// Decode one instruction word (at pc = 5) through a fresh output latch.
ID_EX_Latch run(OperandFetchStage& st, uint32_t ir, bool stall = false, bool flush = false) {
    ID_EX_Latch out = dirtyOut();
    const Regs regs = makeRegs();
    st.clockNext(ifid(ir), out, regs, mcu(), stall, flush);
    return out;
}

} // namespace

// ===========================================================================
// Integer ALU
// ===========================================================================

TEST_CASE(decode_add_register_format) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("add", 3, 1, 2));
    CHECK(o.is_valid);
    CHECK_EQ(o.rd, 3);
    CHECK_EQ(o.op1, 101);
    CHECK_EQ(o.op2, 102);
    CHECK_EQ(o.immx, 0);
    CHECK_EQ(o.pc_next, 6);
    CHECK(sig(o, Signal::IS_ADD)); CHECK(sig(o, Signal::IS_WB));
    CHECK(!sig(o, Signal::IS_IMMEDIATE));
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_add_immediate_format) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("add", 4, 1, 5));
    CHECK(o.is_valid);
    CHECK_EQ(o.rd, 4);
    CHECK_EQ(o.op1, 101);
    CHECK_EQ(o.op2, 0);                     // rs2 field is not a register here
    CHECK_EQ(o.immx, 5);
    CHECK(sig(o, Signal::IS_ADD)); CHECK(sig(o, Signal::IS_IMMEDIATE));
}

TEST_CASE(decode_immediate_sign_extension) {
    OperandFetchStage st;
    CHECK_EQ(run(st, encI("add", 1, 1, static_cast<uint32_t>(-3))).immx, 0xFFFFFFFDu);
    CHECK_EQ(run(st, encI("add", 1, 1, 0x07FFF)).immx, 0x00007FFFu);   // largest positive 16-bit
    CHECK_EQ(run(st, encI("add", 1, 1, 0x08000)).immx, 0xFFFF8000u);   // sign bit (bit 15)
    CHECK_EQ(run(st, encI("add", 1, 1, 0x0FFFF)).immx, 0xFFFFFFFFu);   // -1
}

TEST_CASE(decode_all_two_format_alu_ops_set_their_signal) {
    struct Case { const char* mn; Signal sig; bool writesBack; };
    const Case cases[] = {
        {"add", Signal::IS_ADD, true}, {"sub", Signal::IS_SUB, true},
        {"mul", Signal::IS_MUL, true}, {"div", Signal::IS_DIV, true},
        {"mod", Signal::IS_MOD, true}, {"cmp", Signal::IS_CMP, false},
        {"and", Signal::IS_AND, true}, {"or",  Signal::IS_OR,  true},
        {"xor", Signal::IS_XOR, true}, {"not", Signal::IS_NOT, true},
        {"lsl", Signal::IS_LSL, true}, {"lsr", Signal::IS_LSR, true},
        {"asr", Signal::IS_ASR, true},
    };
    for (const Case& c : cases) {
        OperandFetchStage st;
        ID_EX_Latch r = run(st, encR(c.mn, 2, 3, 4));
        ID_EX_Latch i = run(st, encI(c.mn, 2, 3, 7));
        CHECK(r.is_valid); CHECK(i.is_valid);
        CHECK(sig(r, c.sig)); CHECK(sig(i, c.sig));
        CHECK(!sig(r, Signal::IS_IMMEDIATE)); CHECK(sig(i, Signal::IS_IMMEDIATE));
        CHECK_EQ(sig(r, Signal::IS_WB), c.writesBack);
        CHECK_EQ(sig(i, Signal::IS_WB), c.writesBack);
        CHECK(!st.illegalInstruction());
    }
}

TEST_CASE(decode_not_register_reads_rs2_only) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("not", 2, 0, 7));
    CHECK_EQ(o.op1, 0);                     // rs1 field (r0 = 100) must not be read
    CHECK_EQ(o.op2, 107);
    CHECK_EQ(o.rd, 2);
}

TEST_CASE(decode_not_immediate_reads_no_registers) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("not", 2, 0, 9));
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0); CHECK_EQ(o.immx, 9);
}

TEST_CASE(decode_cmp_register_and_immediate) {
    OperandFetchStage st;
    ID_EX_Latch r = run(st, encR("cmp", 0, 3, 4));
    CHECK_EQ(r.op1, 103); CHECK_EQ(r.op2, 104);
    CHECK(sig(r, Signal::IS_CMP)); CHECK(!sig(r, Signal::IS_WB));

    ID_EX_Latch i = run(st, encI("cmp", 0, 3, 7));
    CHECK_EQ(i.op1, 103); CHECK_EQ(i.op2, 0); CHECK_EQ(i.immx, 7);
}

// ===========================================================================
// Memory and data movement
// ===========================================================================

TEST_CASE(decode_ld) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("ld", 5, 2, 8));
    CHECK_EQ(o.rd, 5);
    CHECK_EQ(o.op1, 102);                   // base register
    CHECK_EQ(o.op2, 0);
    CHECK_EQ(o.immx, 8);
    CHECK(sig(o, Signal::IS_LD)); CHECK(sig(o, Signal::IS_WB));
    CHECK(sig(o, Signal::IS_ADD)); CHECK(sig(o, Signal::IS_IMMEDIATE));
    CHECK(!sig(o, Signal::IS_ST));
}

TEST_CASE(decode_st_carries_value_in_op2) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("st", 6, 2, 4));
    CHECK_EQ(o.op1, 102);                   // base register (rs1)
    CHECK_EQ(o.op2, 106);                   // value to store comes from the rd field
    CHECK_EQ(o.immx, 4);
    CHECK(sig(o, Signal::IS_ST)); CHECK(sig(o, Signal::IS_ADD));
    CHECK(!sig(o, Signal::IS_WB));
}

TEST_CASE(decode_movu_zero_extends_and_reads_nothing) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("movu", 1, 0, 0xFFFF));
    CHECK_EQ(o.rd, 1);
    CHECK_EQ(o.immx, 0x0000FFFFu);          // not sign-extended
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0);
    CHECK(sig(o, Signal::IS_MOVU)); CHECK(sig(o, Signal::IS_WB));
    CHECK(!sig(o, Signal::IS_MOVH));
}

TEST_CASE(decode_movh_shifts_into_upper_half) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encI("movh", 1, 0, 0x1234));
    CHECK_EQ(o.immx, 0x12340000u);
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0);
    CHECK(sig(o, Signal::IS_MOVH)); CHECK(!sig(o, Signal::IS_MOVU));
}

TEST_CASE(decode_csrr_and_csrw) {
    OperandFetchStage st;
    ID_EX_Latch rd = run(st, encI("csrr", 2, 0, 3));
    CHECK_EQ(rd.rd, 2); CHECK_EQ(rd.immx, 3); CHECK_EQ(rd.op1, 0);
    CHECK(sig(rd, Signal::IS_CSRR)); CHECK(sig(rd, Signal::IS_WB));

    ID_EX_Latch wr = run(st, encI("csrw", 0, 4, 2));
    CHECK_EQ(wr.op1, 104); CHECK_EQ(wr.immx, 2);
    CHECK(sig(wr, Signal::IS_CSRW)); CHECK(!sig(wr, Signal::IS_WB));
}

TEST_CASE(decode_chk_reads_two_registers_and_is_not_a_cmp) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("chk", 0, 3, 4));
    CHECK_EQ(o.op1, 103); CHECK_EQ(o.op2, 104);
    CHECK(sig(o, Signal::IS_CHK));
    CHECK(!sig(o, Signal::IS_CMP));         // must not clobber the flags
    CHECK(!sig(o, Signal::IS_WB));
}

TEST_CASE(decode_clrz) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("clrz", 7, 0, 0));
    CHECK_EQ(o.rd, 7);
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0);
    CHECK(sig(o, Signal::IS_CLEAR)); CHECK(sig(o, Signal::IS_WB));
}

// ===========================================================================
// Floating point
// ===========================================================================

TEST_CASE(decode_fpu_arithmetic) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("fadd", 8, 9, 10));
    CHECK_EQ(o.rd, 8); CHECK_EQ(o.op1, 109); CHECK_EQ(o.op2, 110);
    CHECK(sig(o, Signal::IS_FLOAT)); CHECK(sig(o, Signal::IS_FADD)); CHECK(sig(o, Signal::IS_WB));

    ID_EX_Latch c = run(st, encR("fcmp", 0, 9, 10));
    CHECK(sig(c, Signal::IS_FLOAT)); CHECK(sig(c, Signal::IS_FCMP)); CHECK(!sig(c, Signal::IS_WB));
    CHECK_EQ(c.op1, 109); CHECK_EQ(c.op2, 110);
}

TEST_CASE(decode_fmov_signals) {
    // Only the control word is asserted: fmov's operand layout is still open.
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("fmov", 8, 9, 10));
    CHECK(o.is_valid);
    CHECK(sig(o, Signal::IS_FLOAT)); CHECK(sig(o, Signal::IS_WB));
    CHECK(!sig(o, Signal::IS_FADD) && !sig(o, Signal::IS_FSUB) &&
          !sig(o, Signal::IS_FMUL) && !sig(o, Signal::IS_FDIV) && !sig(o, Signal::IS_FCMP));
}

// ===========================================================================
// Control flow
// ===========================================================================

// These three assume the assembler writes branch offsets in BYTES
// (kBranchOffsetInBytes = true in instruction_decode.cpp). If you switch it to
// instruction-index units, drop the "/ 4" expectations below.
TEST_CASE(decode_conditional_branch_offsets) {
    OperandFetchStage st;
    ID_EX_Latch fwd = run(st, encBranch("beq", 8));
    CHECK(sig(fwd, Signal::IS_BEQ)); CHECK(sig(fwd, Signal::IS_IMMEDIATE));
    CHECK_EQ(fwd.immx, 2);
    CHECK_EQ(fwd.op1, 0); CHECK_EQ(fwd.op2, 0);          // branches read no registers

    ID_EX_Latch back = run(st, encBranch("bne", static_cast<uint32_t>(-8)));
    CHECK(sig(back, Signal::IS_BNE));
    CHECK_EQ(back.immx, 0xFFFFFFFEu);                    // -2 instructions

    ID_EX_Latch self = run(st, encBranch("bgt", 0));
    CHECK_EQ(self.immx, 0);
}

TEST_CASE(decode_unconditional_branch) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encBranch("b", 16));
    CHECK(sig(o, Signal::IS_UBRANCH)); CHECK(!sig(o, Signal::IS_RET));
    CHECK_EQ(o.immx, 4);
    CHECK(!sig(o, Signal::IS_WB));
}

TEST_CASE(decode_call_links_into_ra) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encBranch("call", 4));
    CHECK_EQ(o.rd, 15);                                  // ra
    CHECK_EQ(o.immx, 1);
    CHECK(sig(o, Signal::IS_CALL)); CHECK(sig(o, Signal::IS_WB)); CHECK(sig(o, Signal::IS_UBRANCH));
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0);
    CHECK_EQ(o.pc_next, 6);                              // link address
}

TEST_CASE(decode_ret_reads_ra) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("ret", 0, 0, 0));
    CHECK(sig(o, Signal::IS_RET)); CHECK(sig(o, Signal::IS_UBRANCH));
    CHECK_EQ(o.op1, 115);                                // regs[15]
    CHECK_EQ(o.op2, 0);
    CHECK_EQ(o.immx, 0);
}

TEST_CASE(branch_target_arithmetic) {
    // pc_next = pc + 1; target = pc + offset = (pc_next - 1) + offset
    CHECK_EQ(OperandFetchStage::branchTarget(6, 2), 7);
    CHECK_EQ(OperandFetchStage::branchTarget(6, 0xFFFFFFFEu), 3);   // two back
    CHECK_EQ(OperandFetchStage::branchTarget(6, 0), 5);             // branch to itself
    CHECK_EQ(OperandFetchStage::branchTarget(1, 0), 0);
}

// ===========================================================================
// System instructions and nop
// ===========================================================================

TEST_CASE(decode_ecall_reads_r1_and_r2) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("ecall", 0, 0, 0));
    CHECK(sig(o, Signal::IS_SYSCALL));
    CHECK_EQ(o.op1, 101); CHECK_EQ(o.op2, 102);
}

TEST_CASE(decode_eret_ebreak_wfi_read_nothing) {
    const struct { const char* mn; Signal sig; } cases[] = {
        {"eret", Signal::IS_ERET}, {"ebreak", Signal::IS_EBREAK}, {"wfi", Signal::IS_WFI},
    };
    for (const auto& c : cases) {
        OperandFetchStage st;
        ID_EX_Latch o = run(st, encR(c.mn, 0, 0, 0));
        CHECK(o.is_valid);
        CHECK(sig(o, c.sig));
        CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0);
        CHECK(!st.illegalInstruction());
    }
}

TEST_CASE(decode_nop_is_a_clean_valid_bubble) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, 0u);            // all-zero word
    CHECK(o.is_valid);                      // a real instruction, not a pipeline bubble
    CHECK(o.control.bits.none());
    CHECK_EQ(o.rd, 0); CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0); CHECK_EQ(o.immx, 0);
    CHECK_EQ(o.pc_next, 6);
    CHECK(!st.illegalInstruction());
}

// ===========================================================================
// Illegal opcodes
// ===========================================================================

TEST_CASE(decode_unassigned_opcode_is_illegal) {
    OperandFetchStage st;
    const uint32_t ir = (0b0001110u << 25);   // opcode 000111 is not in OPCODE_MAP
    ID_EX_Latch o = run(st, ir);
    CHECK(!o.is_valid);
    CHECK(st.illegalInstruction());
    CHECK_EQ(st.illegalPC(), 5);
    CHECK_EQ(st.illegalIR(), ir);
    st.clearIllegal();
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_wrong_format_bit_is_illegal) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("ld", 1, 2, 3));        // ld only exists as I-format
    CHECK(!o.is_valid);
    CHECK(st.illegalInstruction());
}

TEST_CASE(decode_illegal_flag_stays_until_cleared) {
    OperandFetchStage st;
    run(st, (0b0001110u << 25));
    ID_EX_Latch ok = run(st, encR("add", 1, 2, 3));      // a good instruction after it
    CHECK(ok.is_valid);
    CHECK(st.illegalInstruction());                      // still pending for the trap unit
    st.clearIllegal();
    CHECK(!st.illegalInstruction());
}

// ===========================================================================
// Stall / flush / bubble handling
// ===========================================================================

TEST_CASE(decode_stall_inserts_bubble) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("add", 3, 1, 2), /*stall=*/true);
    CHECK(!o.is_valid);
    CHECK(o.control.bits.none());
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0); CHECK_EQ(o.rd, 0); CHECK_EQ(o.pc_next, 0);
}

TEST_CASE(decode_stall_skips_decode_so_no_illegal_flag) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, (0b0001110u << 25), /*stall=*/true);
    CHECK(!o.is_valid);
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_flush_clears_output) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("add", 3, 1, 2), false, /*flush=*/true);
    CHECK(!o.is_valid);
    CHECK(o.control.bits.none());
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.immx, 0);
}

TEST_CASE(decode_flush_beats_stall_and_illegal) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, (0b0001110u << 25), true, true);
    CHECK(!o.is_valid);
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_bubble_input_gives_bubble_output) {
    OperandFetchStage st;
    IF_ID_Latch in = ifid(encR("add", 3, 1, 2));
    in.is_valid = false;                                  // bubble in IF/ID
    ID_EX_Latch out = dirtyOut();
    const Regs regs = makeRegs();
    st.clockNext(in, out, regs, mcu(), false, false);
    CHECK(!out.is_valid);
    CHECK(out.control.bits.none());
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_bubble_with_illegal_ir_is_ignored) {
    OperandFetchStage st;
    IF_ID_Latch in = ifid((0b0001110u << 25));
    in.is_valid = false;
    ID_EX_Latch out = dirtyOut();
    const Regs regs = makeRegs();
    st.clockNext(in, out, regs, mcu(), false, false);
    CHECK(!out.is_valid);
    CHECK(!st.illegalInstruction());
}

TEST_CASE(decode_overwrites_stale_output_completely) {
    OperandFetchStage st;
    ID_EX_Latch o = run(st, encR("clrz", 7, 0, 0));       // garbage in, clean out
    CHECK_EQ(o.op1, 0); CHECK_EQ(o.op2, 0); CHECK_EQ(o.immx, 0);
    CHECK(sig(o, Signal::IS_CLEAR));
    CHECK(!sig(o, Signal::IS_ADD));                       // garbage had every bit set
}

// ===========================================================================
// peekSources (used by the hazard unit)
// ===========================================================================

TEST_CASE(peek_sources_per_instruction_class) {
    {   // add r3, r1, r2
        OperandSources s = OperandFetchStage::peekSources(ifid(encR("add", 3, 1, 2)), mcu());
        CHECK(s.uses_a); CHECK(s.uses_b); CHECK_EQ(s.a, 1); CHECK_EQ(s.b, 2);
    }
    {   // add with immediate: only rs1
        OperandSources s = OperandFetchStage::peekSources(ifid(encI("add", 3, 1, 5)), mcu());
        CHECK(s.uses_a); CHECK(!s.uses_b); CHECK_EQ(s.a, 1);
    }
    {   // st: base in rs1, value in the rd field
        OperandSources s = OperandFetchStage::peekSources(ifid(encI("st", 6, 2, 4)), mcu());
        CHECK(s.uses_a); CHECK(s.uses_b); CHECK_EQ(s.a, 2); CHECK_EQ(s.b, 6);
    }
    {   // ld: base only
        OperandSources s = OperandFetchStage::peekSources(ifid(encI("ld", 5, 2, 8)), mcu());
        CHECK(s.uses_a); CHECK(!s.uses_b); CHECK_EQ(s.a, 2);
    }
    {   // not (register): rs2 only
        OperandSources s = OperandFetchStage::peekSources(ifid(encR("not", 2, 0, 7)), mcu());
        CHECK(!s.uses_a); CHECK(s.uses_b); CHECK_EQ(s.b, 7);
    }
    {   // chk reads two
        OperandSources s = OperandFetchStage::peekSources(ifid(encR("chk", 0, 3, 4)), mcu());
        CHECK(s.uses_a); CHECK(s.uses_b); CHECK_EQ(s.a, 3); CHECK_EQ(s.b, 4);
    }
    {   // ret reads ra
        OperandSources s = OperandFetchStage::peekSources(ifid(encR("ret", 0, 0, 0)), mcu());
        CHECK(s.uses_a); CHECK(!s.uses_b); CHECK_EQ(s.a, 15);
    }
    {   // ecall reads r1 and r2
        OperandSources s = OperandFetchStage::peekSources(ifid(encR("ecall", 0, 0, 0)), mcu());
        CHECK(s.uses_a); CHECK(s.uses_b); CHECK_EQ(s.a, 1); CHECK_EQ(s.b, 2);
    }
}

TEST_CASE(peek_sources_none_cases) {
    const uint32_t noSource[] = {
        0u,                                   // nop
        encI("movu", 1, 0, 5), encI("movh", 1, 0, 5), encI("csrr", 1, 0, 3),
        encR("clrz", 7, 0, 0), encBranch("beq", 8), encBranch("call", 4),
        encR("eret", 0, 0, 0), encR("wfi", 0, 0, 0), encR("ebreak", 0, 0, 0),
    };
    for (uint32_t ir : noSource) {
        OperandSources s = OperandFetchStage::peekSources(ifid(ir), mcu());
        CHECK(!s.uses_a); CHECK(!s.uses_b);
    }
    IF_ID_Latch bubble = ifid(encR("add", 3, 1, 2));
    bubble.is_valid = false;
    OperandSources b = OperandFetchStage::peekSources(bubble, mcu());
    CHECK(!b.uses_a); CHECK(!b.uses_b);

    OperandSources bad = OperandFetchStage::peekSources(ifid((0b0001110u << 25)), mcu());
    CHECK(!bad.uses_a); CHECK(!bad.uses_b);
}

TEST_CASE(peek_sources_agrees_with_decoded_operands) {
    // For every instruction the operand values the stage reads must match
    // what peekSources told the hazard unit it would read.
    const uint32_t words[] = {
        encR("add", 3, 1, 2), encI("add", 3, 1, 5), encI("st", 6, 2, 4), encI("ld", 5, 2, 8),
        encR("not", 2, 0, 7), encR("chk", 0, 3, 4), encR("ret", 0, 0, 0), 0u,
    };
    const Regs regs = makeRegs();
    for (uint32_t ir : words) {
        OperandFetchStage st;
        OperandSources s = OperandFetchStage::peekSources(ifid(ir), mcu());
        ID_EX_Latch out;
        st.clockNext(ifid(ir), out, regs, mcu(), false, false);
        CHECK_EQ(out.op1, s.uses_a ? regs[s.a] : 0u);
        CHECK_EQ(out.op2, s.uses_b ? regs[s.b] : 0u);
    }
}

// ===========================================================================
// Fetch + decode together
// ===========================================================================

TEST_CASE(integration_fetch_then_decode) {
    std::vector<std::string> prog = { toBits(encR("add", 3, 1, 2)), toBits(encI("ld", 5, 2, 8)) };
    InstructionMemory imem;
    imem.loadProgram(prog);
    InstructionFetchStage f;
    OperandFetchStage d;
    IF_ID_Latch ifidL;
    ID_EX_Latch idex;
    const Regs regs = makeRegs();

    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);
    d.clockNext(ifidL, idex, regs, mcu(), false, false);
    CHECK(idex.is_valid); CHECK(sig(idex, Signal::IS_ADD));
    CHECK_EQ(idex.op1, 101); CHECK_EQ(idex.op2, 102); CHECK_EQ(idex.pc_next, 1);

    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);
    d.clockNext(ifidL, idex, regs, mcu(), false, false);
    CHECK(sig(idex, Signal::IS_LD)); CHECK_EQ(idex.op1, 102); CHECK_EQ(idex.immx, 8);
    CHECK_EQ(idex.pc_next, 2);

    // Program has ended: IF produces a bubble and ID passes it on.
    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);
    d.clockNext(ifidL, idex, regs, mcu(), false, false);
    CHECK(!idex.is_valid);
}

TEST_CASE(integration_stall_holds_instruction_then_decodes_it) {
    std::vector<std::string> prog = { toBits(encR("add", 3, 1, 2)), toBits(encR("sub", 4, 1, 2)) };
    InstructionMemory imem;
    imem.loadProgram(prog);
    InstructionFetchStage f;
    OperandFetchStage d;
    IF_ID_Latch ifidL;
    ID_EX_Latch idex;
    const Regs regs = makeRegs();

    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);   // add in IF/ID

    // Hazard unit stalls: ID inserts a bubble and IF holds its latch and PC.
    d.clockNext(ifidL, idex, regs, mcu(), /*stall=*/true, false);
    f.clockNext(imem, ifidL, /*stall=*/true, false, false, 0, false, 0, false, 0);
    CHECK(!idex.is_valid);
    CHECK_EQ(ifidL.ir, encR("add", 3, 1, 2));
    CHECK_EQ(f.getPC(), 1);

    // Stall released: the held add is decoded now.
    d.clockNext(ifidL, idex, regs, mcu(), false, false);
    CHECK(idex.is_valid); CHECK(sig(idex, Signal::IS_ADD));
}

TEST_CASE(integration_taken_branch_squashes_wrong_path) {
    std::vector<std::string> prog = {
        toBits(encR("add", 1, 2, 3)), toBits(encR("sub", 1, 2, 3)), toBits(encR("mul", 1, 2, 3)),
    };
    InstructionMemory imem;
    imem.loadProgram(prog);
    InstructionFetchStage f;
    OperandFetchStage d;
    IF_ID_Latch ifidL;
    ID_EX_Latch idex;
    const Regs regs = makeRegs();

    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);
    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);   // sub in IF/ID
    // A branch somewhere downstream resolves: flush IF/ID and ID/EX, jump to index 2.
    f.clockNext(imem, ifidL, false, true, true, 2, false, 0, false, 0);
    d.clockNext(ifidL, idex, regs, mcu(), false, true);
    CHECK(!ifidL.is_valid); CHECK(!idex.is_valid);
    CHECK_EQ(f.getPC(), 2);

    f.clockNext(imem, ifidL, false, false, false, 0, false, 0, false, 0);
    d.clockNext(ifidL, idex, regs, mcu(), false, false);
    CHECK(sig(idex, Signal::IS_MUL));                                       // target instruction
}