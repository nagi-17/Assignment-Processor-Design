// four_stage_entry.cpp
// TUI debugger entry point for the 4-stage pipeline.

#include "four_stage_cpu.hpp"

#include <string>
#include <vector>

#include "app.hpp"
#include "cpu_tui_target.hpp"
#include "entry.hpp"

bool runTuiFourStage(const std::vector<std::string>& program) {
    tui::CpuTuiTarget<FourStageCPU> target(program);

    tui::TuiOptions options;
    options.model_name = "4-stage pipeline";
    options.clock_period_ns = FourStageCPU::CLOCK_PERIOD_NS;
    options.program = program;

    return tui::runTuiDebugger(target, options);
}
