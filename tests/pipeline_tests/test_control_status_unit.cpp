#include "test_support.hpp"
#include "control_status_unit.hpp"
#include "instruction_fetch.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFlags    = static_cast<uint32_t>(CsrIndex::Flags);
constexpr uint32_t kEpc      = static_cast<uint32_t>(CsrIndex::Epc);
constexpr uint32_t kCause    = static_cast<uint32_t>(CsrIndex::Cause);
constexpr uint32_t kReserved = 0xFFFFu;

constexpr uint32_t causeOf(ExceptionCause c) {
    return static_cast<uint32_t>(c);
}

InstructionMemory memOf(std::size_t n) {
    std::vector<std::string> program(n, tf::toBits(0u));
    InstructionMemory m;
    m.loadProgram(program);
    return m;
}

}

// ===========================================================================
// Reset / defaults
// ===========================================================================

TEST_CASE(csr_defaults) {
    ControlStatusUnit csr;
    CHECK_EQ(csr.epc(), 0u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::None));
    CHECK(!csr.flagEqual());
    CHECK(!csr.flagGreater());
    CHECK(!csr.halted());
    CHECK(!csr.inTrap());
    CHECK_EQ(csr.trapVector(), ControlStatusUnit::kDefaultTrapVector);
}

TEST_CASE(csr_reset_clears_state_but_keeps_vector) {
    ControlStatusUnit csr(0x40);
    StatusInputs in;
    in.is_ecall = true;
    in.pc = 12;
    in.is_wfi = true;
    in.flag_write = true;
    in.flag_equal = true;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK(csr.halted());

    csr.reset();
    CHECK_EQ(csr.epc(), 0u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::None));
    CHECK(!csr.flagEqual());
    CHECK(!csr.flagGreater());
    CHECK(!csr.halted());
    CHECK(!csr.inTrap());
    CHECK_EQ(csr.trapVector(), 0x40u);      // hardware configuration survives
}

// ===========================================================================
// csrr / csrw
// ===========================================================================

TEST_CASE(csr_write_read_flags) {
    ControlStatusUnit csr;
    uint32_t v = 0xDEADBEEFu;

    CHECK(csr.writeCsr(kFlags, 0x3u));
    CHECK(csr.flagEqual());
    CHECK(csr.flagGreater());
    CHECK(csr.readCsr(kFlags, v));
    CHECK_EQ(v, 3u);

    CHECK(csr.writeCsr(kFlags, 0x0u));
    CHECK(!csr.flagEqual());
    CHECK(!csr.flagGreater());
    CHECK(csr.readCsr(kFlags, v));
    CHECK_EQ(v, 0u);

    CHECK(csr.writeCsr(kFlags, 0x2u));       // GT only
    CHECK(!csr.flagEqual());
    CHECK(csr.flagGreater());
}

TEST_CASE(csr_write_read_epc_and_cause) {
    ControlStatusUnit csr;
    uint32_t v = 0;

    CHECK(csr.writeCsr(kEpc, 0x1234u));
    CHECK(csr.readCsr(kEpc, v)); CHECK_EQ(v, 0x1234u);
    CHECK_EQ(csr.epc(), 0x1234u);

    CHECK(csr.writeCsr(kCause, causeOf(ExceptionCause::Breakpoint)));
    CHECK(csr.readCsr(kCause, v)); CHECK_EQ(v, causeOf(ExceptionCause::Breakpoint));
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Breakpoint));
}

TEST_CASE(csr_reserved_index_rejected_on_read) {
    ControlStatusUnit csr;
    uint32_t v = 0xA5A5A5A5u;
    CHECK(!csr.readCsr(kReserved, v));
    CHECK_EQ(v, 0xA5A5A5A5u);                // untouched on failure
}

TEST_CASE(csr_reserved_index_rejected_on_write) {
    ControlStatusUnit csr;
    csr.writeCsr(kEpc, 7u);
    CHECK(!csr.writeCsr(kReserved, 0xFFFFFFFFu));
    CHECK_EQ(csr.epc(), 7u);                 // state untouched
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::None));
    CHECK(!csr.flagEqual());
}

TEST_CASE(csr_flags_updated_by_clock) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.flag_write   = true;
    in.flag_equal   = true;
    in.flag_greater = false;
    StatusSignals s = csr.clockNext(in);
    CHECK(!s.is_trap);
    CHECK(csr.flagEqual());
    CHECK(!csr.flagGreater());

    uint32_t v = 0;
    CHECK(csr.readCsr(kFlags, v));
    CHECK_EQ(v, 1u);                         // E only

    in.flag_equal   = false;
    in.flag_greater = true;
    csr.clockNext(in);
    CHECK(!csr.flagEqual());
    CHECK(csr.flagGreater());

    // flag_write low: flags hold their previous value
    StatusInputs hold;
    csr.clockNext(hold);
    CHECK(csr.flagGreater());
}

// ===========================================================================
// Traps
// ===========================================================================

TEST_CASE(csr_ecall_raises_syscall_trap) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_ecall = true;
    in.pc = 7;
    StatusSignals s = csr.clockNext(in);

    CHECK(s.is_trap);
    CHECK(!s.is_eret);
    CHECK_EQ(s.trap_vector, csr.trapVector());
    CHECK_EQ(csr.epc(), 7u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Syscall));
    CHECK(csr.inTrap());
}

TEST_CASE(csr_ebreak_raises_breakpoint_trap) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_ebreak = true;
    in.pc = 3;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK_EQ(csr.epc(), 3u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Breakpoint));
}

TEST_CASE(csr_chk_fault_raises_out_of_bounds) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.chk_fault = true;
    in.pc = 9;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK_EQ(csr.epc(), 9u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::OutOfBounds));
}

TEST_CASE(csr_raised_trap_taken_on_next_clock) {
    ControlStatusUnit csr;
    csr.raiseTrap(ExceptionCause::IllegalInstruction, 11);
    CHECK(!csr.inTrap());                    // not taken until the clock edge

    StatusInputs in;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK_EQ(csr.epc(), 11u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::IllegalInstruction));
    CHECK(csr.inTrap());
}

TEST_CASE(csr_pending_trap_is_consumed_once) {
    ControlStatusUnit csr;
    csr.raiseTrap(ExceptionCause::IllegalInstruction, 2);
    StatusInputs in;
    CHECK(csr.clockNext(in).is_trap);
    StatusInputs e; e.is_eret = true;
    csr.clockNext(e);                        // leave the handler
    CHECK(!csr.clockNext(in).is_trap);       // the trap does not re-fire
}

TEST_CASE(csr_trap_priority_pending_beats_ecall) {
    ControlStatusUnit csr;
    csr.raiseTrap(ExceptionCause::IllegalInstruction, 11);
    StatusInputs in;
    in.is_ecall  = true;
    in.chk_fault = true;
    in.pc = 4;
    CHECK(csr.clockNext(in).is_trap);
    CHECK_EQ(csr.epc(), 11u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::IllegalInstruction));
}

TEST_CASE(csr_trap_priority_ecall_beats_ebreak_and_chk) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_ecall  = true;
    in.is_ebreak = true;
    in.chk_fault = true;
    in.pc = 8;
    CHECK(csr.clockNext(in).is_trap);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Syscall));
}

TEST_CASE(csr_trap_priority_ebreak_beats_chk) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_ebreak = true;
    in.chk_fault = true;
    in.pc = 8;
    CHECK(csr.clockNext(in).is_trap);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Breakpoint));
}

TEST_CASE(csr_no_trap_inputs_yields_nothing) {
    ControlStatusUnit csr;
    StatusInputs in;
    StatusSignals s = csr.clockNext(in);
    CHECK(!s.is_trap);
    CHECK(!s.is_eret);
    CHECK(!csr.inTrap());
}

// ===========================================================================
// eret
// ===========================================================================

TEST_CASE(csr_eret_returns_to_epc) {
    ControlStatusUnit csr;
    StatusInputs t;
    t.is_ebreak = true;
    t.pc = 5;
    CHECK(csr.clockNext(t).is_trap);
    CHECK(csr.inTrap());

    StatusInputs e;
    e.is_eret = true;
    StatusSignals s = csr.clockNext(e);
    CHECK(s.is_eret);
    CHECK(!s.is_trap);
    CHECK_EQ(s.epc, 5u);
    CHECK(!csr.inTrap());
}

TEST_CASE(csr_eret_outside_trap_still_redirects) {
    ControlStatusUnit csr;
    csr.writeCsr(kEpc, 42u);
    StatusInputs e;
    e.is_eret = true;
    StatusSignals s = csr.clockNext(e);
    CHECK(s.is_eret);
    CHECK_EQ(s.epc, 42u);
}

TEST_CASE(csr_trap_beats_eret_in_same_cycle) {
    // Mirrors InstructionFetchStage priority: trap > eret.
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_eret  = true;
    in.is_ecall = true;
    in.pc = 1;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK(!s.is_eret);
    CHECK(csr.inTrap());
}

// ===========================================================================
// Nested traps
// ===========================================================================

TEST_CASE(csr_nested_trap_suppressed) {
    ControlStatusUnit csr;
    StatusInputs a;
    a.is_ecall = true;
    a.pc = 3;
    CHECK(csr.clockNext(a).is_trap);

    StatusInputs b;
    b.is_ebreak = true;
    b.pc = 9;
    StatusSignals s = csr.clockNext(b);
    CHECK(!s.is_trap);                       // dropped while handling
    CHECK_EQ(csr.epc(), 3u);                 // unchanged
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Syscall));
    CHECK(csr.inTrap());
}

TEST_CASE(csr_handler_can_resume_handling_after_eret) {
    ControlStatusUnit csr;
    StatusInputs a; a.is_ecall = true; a.pc = 3;
    csr.clockNext(a);

    StatusInputs e; e.is_eret = true;
    csr.clockNext(e);
    CHECK(!csr.inTrap());

    StatusInputs b; b.is_ebreak = true; b.pc = 9;
    StatusSignals s = csr.clockNext(b);
    CHECK(s.is_trap);                        // accepted again
    CHECK_EQ(csr.epc(), 9u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::Breakpoint));
}

// ===========================================================================
// wfi
// ===========================================================================

TEST_CASE(csr_wfi_halts_until_wake) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_wfi = true;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.halted);
    CHECK(csr.halted());

    StatusInputs none;
    CHECK(csr.clockNext(none).halted);       // latched

    csr.wake();
    CHECK(!csr.halted());
    CHECK(!csr.clockNext(none).halted);
}

TEST_CASE(csr_halt_does_not_block_other_signals) {
    ControlStatusUnit csr;
    StatusInputs in;
    in.is_wfi   = true;
    in.is_ecall = true;
    in.pc = 6;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.halted);
    CHECK(s.is_trap);
    CHECK_EQ(csr.epc(), 6u);
}

// ===========================================================================
// Custom trap vector
// ===========================================================================

TEST_CASE(csr_custom_trap_vector) {
    ControlStatusUnit csr(0x1234u);
    CHECK_EQ(csr.trapVector(), 0x1234u);
    StatusInputs in;
    in.chk_fault = true;
    in.pc = 2;
    StatusSignals s = csr.clockNext(in);
    CHECK(s.is_trap);
    CHECK_EQ(s.trap_vector, 0x1234u);
    CHECK_EQ(csr.cause(), causeOf(ExceptionCause::OutOfBounds));
}

// ===========================================================================
// Integration with InstructionFetchStage
// ===========================================================================

TEST_CASE(csr_drives_fetch_trap_then_eret) {
    InstructionMemory mem = memOf(8);
    InstructionFetchStage fetch;
    IF_ID_Latch latch;
    ControlStatusUnit csr;

    // ecall at index 0 -> fetch redirects to the trap vector, latch flushed.
    StatusInputs t;
    t.is_ecall = true;
    t.pc = 0;
    StatusSignals s = csr.clockNext(t);
    fetch.clockNext(mem, latch, false, false, false, 0,
                    s.is_eret, s.epc, s.is_trap, s.trap_vector);
    CHECK_EQ(fetch.getPC(), csr.trapVector());
    CHECK(!latch.is_valid);

    // eret -> fetch redirects back to epc (the trapping instruction).
    StatusInputs e;
    e.is_eret = true;
    StatusSignals s2 = csr.clockNext(e);
    CHECK(s2.is_eret);
    fetch.clockNext(mem, latch, false, false, false, 0,
                    s2.is_eret, s2.epc, s2.is_trap, s2.trap_vector);
    CHECK_EQ(fetch.getPC(), 0u);
    CHECK(!latch.is_valid);

    // Fetch resumes normally at epc.
    StatusInputs none;
    StatusSignals s3 = csr.clockNext(none);
    fetch.clockNext(mem, latch, false, false, false, 0,
                    s3.is_eret, s3.epc, s3.is_trap, s3.trap_vector);
    CHECK(latch.is_valid);
    CHECK_EQ(latch.pc, 0u);
    CHECK_EQ(fetch.getPC(), 1u);
}
