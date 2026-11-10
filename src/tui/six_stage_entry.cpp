// six_stage_entry.cpp
// TUI debugger entry point for the 6-stage pipeline.

#include "six_stage_cpu.hpp"

#include <string>
#include <vector>

#include "app.hpp"
#include "cpu_tui_target.hpp"
#include "entry.hpp"

bool runTuiSixStage(const std::vector<std::string>& program) {
    tui::CpuTuiTarget<SixStageCPU> target(program);

    tui::TuiOptions options;
    options.model_name = "6-stage pipeline";
    options.clock_period_ns = SixStageCPU::CLOCK_PERIOD_NS;
    options.program = program;

    return tui::runTuiDebugger(target, options);
}
