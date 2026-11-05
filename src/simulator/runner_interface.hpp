// runner_interface.hpp
// Interface between the top-level main CLI and the 4-stage / 6-stage runners.

#ifndef RUNNER_INTERFACE_HPP
#define RUNNER_INTERFACE_HPP

#include <cstdint>
#include <string>
#include <vector>

struct PipelineStats {
    std::string pipeline_name;
    uint64_t total_cycles{0};
    uint64_t stall_cycles{0};
    uint64_t instructions_retired{0};
    double clock_period_ns{0.0};
    double total_time_ns{0.0};
    double cpi{0.0};
    bool completed{false};
    std::string error;
};

// Run the program on the 4-stage pipeline.
// If debug_mode is true, enters the interactive CLI debugger.
// If debug_mode is false, runs to completion and collects stats.
PipelineStats runFourStage(const std::vector<std::string>& binary, bool debug_mode);

// Run the program on the 6-stage pipeline.
// If debug_mode is true, enters the interactive CLI debugger.
// If debug_mode is false, runs to completion and collects stats.
PipelineStats runSixStage(const std::vector<std::string>& binary, bool debug_mode);

#endif // RUNNER_INTERFACE_HPP
