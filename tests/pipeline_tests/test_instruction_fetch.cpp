#include "test_support.hpp"
#include "instruction_fetch.hpp"

#include <sstream>

using namespace tf;

namespace {

// All the control inputs of InstructionFetchStage::clockNext in one place.
struct Ctl {
    bool stall{false};
    bool flush{false};
    bool branch{false};
    uint32_t target{0};
    bool eret{false};
    uint32_t epc{0};
    bool trap{false};
    uint32_t vec{0};
};

void step(InstructionFetchStage& f, const InstructionMemory& m, IF_ID_Latch& l,
          const Ctl& c = Ctl()) {
    f.clockNext(m, l, c.stall, c.flush, c.branch, c.target, c.eret, c.epc, c.trap, c.vec);
}

InstructionMemory makeMem(const std::vector<uint32_t>& words) {
    std::vector<std::string> p;
    for (uint32_t w : words) p.push_back(toBits(w));
    InstructionMemory m;
    m.loadProgram(p);
    return m;
}

InstructionMemory mem3() {
    return makeMem({0x11111111u, 0x22222222u, 0x33333333u});
}

// A latch holding a valid-looking instruction, to prove it is kept or cleared.
IF_ID_Latch staleLatch() {
    IF_ID_Latch l;
    l.pc = 7;
    l.ir = 0xDEADBEEFu;
    l.pc_next = 8;
    l.is_valid = true;
    return l;
}

// Redirects std::cerr for the lifetime of the object.
struct CerrCapture {
    std::ostringstream buf;
    std::streambuf* old;
    CerrCapture() : old(std::cerr.rdbuf(buf.rdbuf())) {}
    ~CerrCapture() { std::cerr.rdbuf(old); }
    std::string str() const { return buf.str(); }
};

} // namespace

// ===========================================================================
// InstructionMemory
// ===========================================================================

TEST_CASE(imem_load_and_size) {
    InstructionMemory m;
    CHECK_EQ(m.size(), 0);
    m.loadProgram({toBits(1), toBits(2), toBits(3)});
    CHECK_EQ(m.size(), 3);
    m.loadProgram({toBits(9)});            // loading replaces the old program
    CHECK_EQ(m.size(), 1);
}

TEST_CASE(imem_read_valid_words) {
    InstructionMemory m = makeMem({0x00000001u, 0xFFFFFFFFu, 0x80000000u, 0xA5A5A5A5u});
    uint32_t ir = 0;
    CHECK(m.readInstruction(0, ir)); CHECK_EQ(ir, 0x00000001u);
    CHECK(m.readInstruction(1, ir)); CHECK_EQ(ir, 0xFFFFFFFFu);
    CHECK(m.readInstruction(2, ir)); CHECK_EQ(ir, 0x80000000u);   // MSB is bit 31
    CHECK(m.readInstruction(3, ir)); CHECK_EQ(ir, 0xA5A5A5A5u);
}

TEST_CASE(imem_read_out_of_range_fails) {
    InstructionMemory m = mem3();
    uint32_t ir = 0x1234;
    CHECK(!m.readInstruction(3, ir));
    CHECK(!m.readInstruction(1000, ir));
    CHECK_EQ(ir, 0x1234u);                 // untouched on failure
}

TEST_CASE(imem_read_empty_memory_fails) {
    InstructionMemory m;
    uint32_t ir = 0;
    CHECK(!m.readInstruction(0, ir));
}

TEST_CASE(imem_read_short_string_fails) {
    InstructionMemory m;
    m.loadProgram({std::string(31, '1'), std::string()});
    uint32_t ir = 0;
    CHECK(!m.readInstruction(0, ir));
    CHECK(!m.readInstruction(1, ir));
}

TEST_CASE(imem_read_bad_characters_fail) {
    std::string withTwo = toBits(0);       withTwo[10] = '2';
    std::string withSpace = toBits(0);     withSpace[5] = ' ';
    std::string withX = "0x" + toBits(0).substr(2);
    InstructionMemory m;
    m.loadProgram({withTwo, withSpace, withX});
    uint32_t ir = 0;
    CHECK(!m.readInstruction(0, ir));
    CHECK(!m.readInstruction(1, ir));
    CHECK(!m.readInstruction(2, ir));
}

TEST_CASE(imem_read_ignores_trailing_carriage_return) {
    // Lenient by design: only the first 32 characters are inspected, so a
    // Windows line ending left on the string does not break fetching.
    InstructionMemory m;
    m.loadProgram({toBits(0x12345678u) + "\r"});
    uint32_t ir = 0;
    CHECK(m.readInstruction(0, ir));
    CHECK_EQ(ir, 0x12345678u);
}

// ===========================================================================
// InstructionFetchStage: normal fetching
// ===========================================================================

TEST_CASE(fetch_sequential_then_end_of_program) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l;
    CHECK_EQ(f.getPC(), 0);

    step(f, m, l);
    CHECK(l.is_valid); CHECK_EQ(l.pc, 0); CHECK_EQ(l.ir, 0x11111111u); CHECK_EQ(l.pc_next, 1);
    CHECK_EQ(f.getPC(), 1);

    step(f, m, l);
    CHECK(l.is_valid); CHECK_EQ(l.pc, 1); CHECK_EQ(l.ir, 0x22222222u); CHECK_EQ(l.pc_next, 2);

    step(f, m, l);
    CHECK(l.is_valid); CHECK_EQ(l.pc, 2); CHECK_EQ(l.ir, 0x33333333u); CHECK_EQ(l.pc_next, 3);
    CHECK_EQ(f.getPC(), 3);

    // Past the end: the pipeline drains and the PC stops moving.
    step(f, m, l);
    CHECK(!l.is_valid); CHECK_EQ(l.ir, 0);
    CHECK_EQ(f.getPC(), 3);
    step(f, m, l);
    CHECK(!l.is_valid);
    CHECK_EQ(f.getPC(), 3);
}

TEST_CASE(fetch_start_pc_and_setpc) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(2);
    IF_ID_Latch l;
    CHECK_EQ(f.getPC(), 2);
    step(f, m, l);
    CHECK_EQ(l.pc, 2); CHECK_EQ(l.ir, 0x33333333u);

    f.setPC(1);
    CHECK_EQ(f.getPC(), 1);
    step(f, m, l);
    CHECK_EQ(l.pc, 1); CHECK_EQ(l.ir, 0x22222222u);
}

TEST_CASE(fetch_empty_program_gives_bubbles) {
    InstructionMemory m;
    InstructionFetchStage f;
    IF_ID_Latch l = staleLatch();
    step(f, m, l);
    CHECK(!l.is_valid);
    CHECK_EQ(f.getPC(), 0);
}

TEST_CASE(fetch_end_of_program_clears_stale_latch) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(3);
    IF_ID_Latch l = staleLatch();
    step(f, m, l);
    CHECK(!l.is_valid); CHECK_EQ(l.ir, 0); CHECK_EQ(l.pc, 0);
    CHECK_EQ(f.getPC(), 3);
}

// ===========================================================================
// InstructionFetchStage: stall and flush
// ===========================================================================

TEST_CASE(fetch_stall_holds_pc_and_latch) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(1);
    IF_ID_Latch l = staleLatch();
    Ctl c; c.stall = true;
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 1);
    CHECK(l.is_valid); CHECK_EQ(l.ir, 0xDEADBEEFu); CHECK_EQ(l.pc, 7); CHECK_EQ(l.pc_next, 8);
}

TEST_CASE(fetch_stall_then_resume) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l;
    step(f, m, l);                          // fetched index 0, PC -> 1
    Ctl c; c.stall = true;
    step(f, m, l, c);
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 1);
    CHECK_EQ(l.ir, 0x11111111u);            // still holding instruction 0
    step(f, m, l);                          // stall released
    CHECK_EQ(l.pc, 1); CHECK_EQ(l.ir, 0x22222222u);
}

TEST_CASE(fetch_flush_clears_latch_and_keeps_pc) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(2);
    IF_ID_Latch l = staleLatch();
    Ctl c; c.flush = true;
    step(f, m, l, c);
    CHECK(!l.is_valid); CHECK_EQ(l.ir, 0); CHECK_EQ(l.pc, 0); CHECK_EQ(l.pc_next, 0);
    CHECK_EQ(f.getPC(), 2);
}

TEST_CASE(fetch_flush_beats_stall) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(1);
    IF_ID_Latch l = staleLatch();
    Ctl c; c.flush = true; c.stall = true;
    step(f, m, l, c);
    CHECK(!l.is_valid);
    CHECK_EQ(f.getPC(), 1);
}

// ===========================================================================
// InstructionFetchStage: redirects (branch / eret / trap)
// ===========================================================================

TEST_CASE(fetch_branch_taken_flushes_and_redirects) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l = staleLatch();
    Ctl c; c.branch = true; c.target = 2;
    step(f, m, l, c);
    CHECK(!l.is_valid);                     // no wrong-path instruction latched
    CHECK_EQ(f.getPC(), 2);
    step(f, m, l);                          // fetching resumes at the target
    CHECK(l.is_valid); CHECK_EQ(l.pc, 2); CHECK_EQ(l.ir, 0x33333333u);
    CHECK_EQ(f.getPC(), 3);
}

TEST_CASE(fetch_branch_with_explicit_flush_is_same) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l = staleLatch();
    Ctl c; c.branch = true; c.target = 1; c.flush = true;
    step(f, m, l, c);
    CHECK(!l.is_valid);
    CHECK_EQ(f.getPC(), 1);
}

TEST_CASE(fetch_redirect_beats_stall) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l = staleLatch();
    Ctl c; c.stall = true; c.branch = true; c.target = 2;
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 2);
    CHECK(!l.is_valid);
}

TEST_CASE(fetch_eret_redirects_to_epc) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l = staleLatch();
    Ctl c; c.eret = true; c.epc = 2;
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 2);
    CHECK(!l.is_valid);
}

TEST_CASE(fetch_trap_redirects_to_vector) {
    InstructionMemory m = mem3();
    InstructionFetchStage f(2);
    IF_ID_Latch l = staleLatch();
    Ctl c; c.trap = true; c.vec = 1;
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 1);
    CHECK(!l.is_valid);
}

TEST_CASE(fetch_priority_trap_over_eret_over_branch) {
    InstructionMemory m = mem3();
    {   // trap + eret + branch -> trap
        InstructionFetchStage f;
        IF_ID_Latch l;
        Ctl c; c.trap = true; c.vec = 1; c.eret = true; c.epc = 2; c.branch = true; c.target = 0;
        step(f, m, l, c);
        CHECK_EQ(f.getPC(), 1);
    }
    {   // eret + branch -> eret
        InstructionFetchStage f;
        IF_ID_Latch l;
        Ctl c; c.eret = true; c.epc = 2; c.branch = true; c.target = 0;
        step(f, m, l, c);
        CHECK_EQ(f.getPC(), 2);
    }
    {   // trap + flush -> still the trap vector
        InstructionFetchStage f;
        IF_ID_Latch l;
        Ctl c; c.trap = true; c.vec = 1; c.flush = true;
        step(f, m, l, c);
        CHECK_EQ(f.getPC(), 1);
    }
}

TEST_CASE(fetch_branch_target_past_end_drains) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l;
    Ctl c; c.branch = true; c.target = 10;
    step(f, m, l, c);
    CHECK_EQ(f.getPC(), 10);
    step(f, m, l);
    CHECK(!l.is_valid);
    CHECK_EQ(f.getPC(), 10);
}

TEST_CASE(fetch_branch_to_self_loops) {
    InstructionMemory m = mem3();
    InstructionFetchStage f;
    IF_ID_Latch l;
    step(f, m, l);                          // fetch index 0
    Ctl c; c.branch = true; c.target = 0;
    step(f, m, l, c);                       // taken branch back to 0
    CHECK_EQ(f.getPC(), 0);
    step(f, m, l);
    CHECK_EQ(l.pc, 0); CHECK_EQ(l.ir, 0x11111111u);
}

// ===========================================================================
// InstructionFetchStage: malformed instruction memory
// ===========================================================================

TEST_CASE(fetch_bad_instruction_reports_and_does_not_advance) {
    InstructionMemory m;
    m.loadProgram({toBits(0x11111111u), std::string(32, '2'), std::string("101")});
    InstructionFetchStage f;
    IF_ID_Latch l;
    CerrCapture cap;

    step(f, m, l);                          // index 0 is fine
    CHECK(l.is_valid); CHECK_EQ(f.getPC(), 1);

    step(f, m, l);                          // index 1: bad characters
    CHECK(!l.is_valid); CHECK_EQ(l.ir, 0);
    CHECK_EQ(f.getPC(), 1);                 // PC does not move past it
    CHECK(cap.str().find("IMEM ERROR") != std::string::npos);

    step(f, m, l);                          // still stuck on the same index
    CHECK(!l.is_valid); CHECK_EQ(f.getPC(), 1);

    f.setPC(2);                             // index 2: too short
    step(f, m, l);
    CHECK(!l.is_valid); CHECK_EQ(f.getPC(), 2);
}