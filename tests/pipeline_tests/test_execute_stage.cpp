// test_execute_stage.cpp
// Multi-cycle timing tests for the Execute stage.
//
// Compiled without -DFOUR_STAGE_PIPELINE, so ExOutLatch is EX_MA1_Latch.
// Verifies that mul/div/mod/fmul/fdiv hold EX for their latency (emitting a
// bubble each cycle) and only write the result on the final cycle, while
// single-cycle operations still complete immediately.

#include "test_support.hpp"

#include "execute_stage.hpp"
#include "control_status_unit.hpp"
#include "control_signals.hpp"
#include "alu.hpp"

namespace {

// Build a valid ID/EX latch for a given op.
ID_EX_Latch makeLatch(uint32_t op1, uint32_t op2, Signal primary,
                      Signal secondary = Signal::COUNT, bool fp = false) {
    ID_EX_Latch l;
    l.is_valid = true;
    l.op1 = op1;
    l.op2 = op2;
    l.control.bits.reset();
    l.control[Signal::IS_WB] = true;
    if (fp) l.control[Signal::IS_FLOAT] = true;
    if (primary != Signal::COUNT) l.control[primary] = true;
    if (secondary != Signal::COUNT) l.control[secondary] = true;
    return l;
}

// Drive clockNext with a held latch until the stage retires the instruction.
// Returns the 1-based cycle the result appeared on (or 0 if it never did).
int runToCompletion(ExecuteStage& ex, const ID_EX_Latch& in,
                    ExOutLatch& out, int& busy_cycles, uint32_t& value) {
    busy_cycles = 0;
    value = 0;
    for (int cycle = 1; cycle <= 64; ++cycle) {
        ExOut r = ex.clockNext(in, out);
        if (r.busy) {
            ++busy_cycles;
            CHECK(!out.is_valid);          // bubbles while occupied
            continue;
        }
        if (out.is_valid) {
            value = out.exResult;
            return cycle;
        }
        // Single-cycle non-writing instruction (e.g. branch) retired without result.
        return cycle;
    }
    return 0;
}

} // namespace

TEST_CASE(execute_latency_values) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);

    CHECK_EQ(ex.latency(makeLatch(6, 7, Signal::IS_MUL)), 4u);
    CHECK_EQ(ex.latency(makeLatch(7, 6, Signal::IS_DIV)), 16u);
    CHECK_EQ(ex.latency(makeLatch(7, 6, Signal::IS_MOD)), 16u);
    CHECK_EQ(ex.latency(makeLatch(0x3F800000, 0x40000000, Signal::IS_FMUL, Signal::COUNT, true)), 4u);
    CHECK_EQ(ex.latency(makeLatch(0x3F800000, 0x40000000, Signal::IS_FDIV, Signal::COUNT, true)), 6u);
    CHECK_EQ(ex.latency(makeLatch(1, 2, Signal::IS_ADD)), 1u);

    ID_EX_Latch bubble;                    // invalid latch = single cycle
    CHECK_EQ(ex.latency(bubble), 1u);
}

TEST_CASE(execute_single_cycle_add_is_immediate) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;

    ID_EX_Latch in = makeLatch(3, 4, Signal::IS_ADD);
    ExOut r = ex.clockNext(in, out);
    CHECK(!r.busy);
    CHECK(out.is_valid);
    CHECK_EQ(out.exResult, 7u);
    CHECK(!ex.isBusy());
}

TEST_CASE(execute_mul_takes_4_cycles) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;

    ID_EX_Latch in = makeLatch(6, 7, Signal::IS_MUL);
    int busy = 0;
    uint32_t value = 0;
    int done = runToCompletion(ex, in, out, busy, value);
    CHECK_EQ(done, 4);
    CHECK_EQ(busy, 3);
    CHECK_EQ(value, 42u);
    CHECK(!ex.isBusy());                   // released after completion
}

TEST_CASE(execute_div_takes_16_cycles) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;
    ALU alu;

    ID_EX_Latch in = makeLatch(100, 7, Signal::IS_DIV);
    int busy = 0;
    uint32_t value = 0;
    int done = runToCompletion(ex, in, out, busy, value);
    CHECK_EQ(done, 16);
    CHECK_EQ(busy, 15);
    CHECK_EQ(value, alu.execute(100, 7, ALU::OP_DIV).res);
}

TEST_CASE(execute_mod_takes_16_cycles) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;
    ALU alu;

    ID_EX_Latch in = makeLatch(100, 7, Signal::IS_MOD);
    int busy = 0;
    uint32_t value = 0;
    int done = runToCompletion(ex, in, out, busy, value);
    CHECK_EQ(done, 16);
    CHECK_EQ(busy, 15);
    CHECK_EQ(value, alu.execute(100, 7, ALU::OP_MOD).res);
}

TEST_CASE(execute_fmul_takes_4_cycles) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;

    // 2.0 * 3.0
    ID_EX_Latch in = makeLatch(0x40000000, 0x40400000, Signal::IS_FMUL, Signal::COUNT, true);
    int busy = 0;
    uint32_t value = 0;
    int done = runToCompletion(ex, in, out, busy, value);
    CHECK_EQ(done, 4);
    CHECK_EQ(busy, 3);
    CHECK_EQ(value, 0x40C00000u);          // 6.0
}

TEST_CASE(execute_fdiv_takes_6_cycles) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;

    // 6.0 / 2.0
    ID_EX_Latch in = makeLatch(0x40C00000, 0x40000000, Signal::IS_FDIV, Signal::COUNT, true);
    int busy = 0;
    uint32_t value = 0;
    int done = runToCompletion(ex, in, out, busy, value);
    CHECK_EQ(done, 6);
    CHECK_EQ(busy, 5);
    CHECK_EQ(value, 0x40400000u);          // 3.0
}

TEST_CASE(execute_reset_clears_busy) {
    ControlStatusUnit csu;
    ExecuteStage ex(csu);
    ExOutLatch out;

    ID_EX_Latch in = makeLatch(6, 7, Signal::IS_MUL);
    ex.clockNext(in, out);                 // capture (busy)
    CHECK(ex.isBusy());
    ex.reset();
    CHECK(!ex.isBusy());
}

int main() {
    return tf::runAll();
}
