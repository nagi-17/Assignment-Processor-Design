#ifndef RISC201_TUI_APP_HPP
#define RISC201_TUI_APP_HPP

// ---------------------------------------------------------------------------
// Event loop for the TUI debugger.
//
// `runTuiDebugger` owns the terminal, drives the target one clock at a time,
// routes keys and commands through the model, and asks the renderer for a
// frame.  It behaves identically for every pipeline because it only talks to
// the TuiTarget interface.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "debugger.hpp"
#include "debugger_model.hpp"

namespace tui {

// DebuggerTarget plus the one extra operation the TUI needs: rewinding to the
// start of the loaded program.  The pipelines already implement this as
// loadProgram(); this exposes it without leaking a pipeline-specific type.
class TuiTarget : public DebuggerTarget {
public:
    ~TuiTarget() override = default;
    virtual void reload() = 0;
};

struct TuiOptions {
    std::string model_name{"6-stage pipeline"};
    double      clock_period_ns{0.0};
    std::vector<std::string> program;
    bool        force_no_colour{false};
};

// Runs the TUI until the user quits.  Returns false when the terminal could
// not be initialised (not a TTY), so callers can fall back to the CLI.
bool runTuiDebugger(TuiTarget& cpu, const TuiOptions& options);

// Renders one frame without a terminal.  Used by tests and by NO_TUI dumping.
std::string renderOnce(DebuggerTarget& cpu, const TuiOptions& options,
                       const TuiState& state, int width, int height);

} // namespace tui

#endif // RISC201_TUI_APP_HPP
