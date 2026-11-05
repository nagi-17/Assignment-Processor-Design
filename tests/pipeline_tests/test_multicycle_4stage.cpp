// test_multicycle_4stage.cpp
// Integration test: multi-cycle mul/div/mod through the 4-stage pipeline.
// Built like src/simulator/four_stage_runner.cpp (-DFOUR_STAGE_PIPELINE).

#ifndef FOUR_STAGE_PIPELINE
#define FOUR_STAGE_PIPELINE
#endif

#include "pipeline/execute/execute_stage.cpp"
#include "four_stage_cpu.hpp"

#include "test_multicycle_program.hpp"

int main() {
    int fails = 0;
    fails += mc::runAndCheck<FourStageCPU>("4-stage");
    fails += mc::runTailAndCheck<FourStageCPU>("4-stage");
    return fails == 0 ? 0 : 1;
}
