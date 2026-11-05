// test_multicycle_6stage.cpp
// Integration test: multi-cycle mul/div/mod through the 6-stage pipeline.
// Built like src/simulator/six_stage_runner.cpp (no -DFOUR_STAGE_PIPELINE).

#include "pipeline/execute/execute_stage.cpp"
#include "six_stage_cpu.hpp"

#include "test_multicycle_program.hpp"

int main() {
    int fails = 0;
    fails += mc::runAndCheck<SixStageCPU>("6-stage");
    fails += mc::runTailAndCheck<SixStageCPU>("6-stage");
    return fails == 0 ? 0 : 1;
}
