// six_stage_runner.cpp
// Implementation of runSixStage for the 6-stage pipeline processor.

#include <iostream>
#include <iomanip>
#include "runner_interface.hpp"
#include "pipeline/execute/execute_stage.cpp"
#include "six_stage_cpu.hpp"
#include "debugger_cli.hpp"

PipelineStats runSixStage(const std::vector<std::string>& binary, bool debug_mode) {
    SixStageCPU cpu;
    cpu.loadProgram(binary);

    if (debug_mode) {
        std::cout << "\n=======================================================\n";
        std::cout << "  Starting 6-Stage Pipeline Interactive Debugger\n";
        std::cout << "  Type 'help' for commands, 'step' to step, 'run' to run\n";
        std::cout << "=======================================================\n\n";
        runDebuggerCLI(cpu);
    } else {
        const uint64_t max_cycles = 1000000;
        while (!cpu.getSnapshot().is_stopped && cpu.totalCycles() < max_cycles) {
            if (!cpu.stepCycle()) break;
        }
    }

    PipelineStats stats;
    stats.pipeline_name = "6-Stage Pipeline";
    stats.total_cycles = cpu.totalCycles();
    stats.stall_cycles = cpu.stallCycles();
    stats.instructions_retired = cpu.instructionsRetired();
    stats.clock_period_ns = cpu.clockPeriodNs();
    stats.total_time_ns = static_cast<double>(stats.total_cycles) * stats.clock_period_ns;
    stats.cpi = (stats.instructions_retired > 0)
        ? static_cast<double>(stats.total_cycles) / stats.instructions_retired
        : 0.0;
    stats.completed = cpu.getSnapshot().is_stopped;
    return stats;
}
