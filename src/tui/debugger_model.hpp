#ifndef RISC201_TUI_DEBUGGER_MODEL_HPP
#define RISC201_TUI_DEBUGGER_MODEL_HPP

// ---------------------------------------------------------------------------
// Terminal-independent state machine for the TUI debugger.
//
// Everything here is pure logic: view selection, breakpoint bookkeeping,
// command parsing and value formatting.  No terminal and no CPU access, so the
// whole thing is unit-testable and the renderer only has to draw what this
// module produces.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "debugger.hpp"

namespace tui {

// One selectable inspection mode.  Exactly one is visible at a time, which is
// what keeps the screen from turning into a wall of panels.
enum class View {
    Overview,    // register file + flags/CSR peek
    Memory,      // windowed data-memory browser
    Stack,       // active frame between FP and SP
    System,      // epc / cause / flags
    Stats,       // cycles, stalls, retired, CPI, wall-clock
    Breaks,      // breakpoint list
    Help         // command reference
};

const char* viewName(View view);
bool parseViewName(const std::string& text, View& view);

// What the event loop should do next after a command or key press.
enum class ActionKind {
    None,
    Step,
    Continue,     // run until a breakpoint or the CPU stops
    RunToEnd,     // run until the CPU stops, ignoring breakpoints
    Reload,       // reset the model and reload the program
    Quit,
    Redraw
};

struct Action {
    ActionKind kind{ActionKind::None};
    std::uint64_t count{1};
};

struct TuiState {
    View view{View::Overview};

    std::vector<std::uint32_t> breakpoints;

    // Internal address breakpoints are polled against snapshot.pc, because the
    // CPU classes only latch their own (unused) breakpoint list.
    bool     paused_at_breakpoint{false};
    std::uint32_t paused_address{0};
    bool     skip_breakpoint_once{false};

    std::string message;
    bool     message_is_error{false};

    // Command line editing.
    bool        command_active{false};
    std::string command_buffer;

    // View scroll state.
    std::uint32_t memory_base{0};
    std::uint32_t stack_extra_words{0};
    int          scroll{0};

    // True while the event loop is fast-forwarding; drives the header badge.
    bool running{false};
    bool halted{false};

    // "finish" runs to completion while keeping the user's breakpoint list.
    bool ignore_breakpoints{false};

    // Wall-clock time since the session opened, in milliseconds.
    double session_ms{0.0};

    void setMessage(std::string text, bool error = false) {
        message = std::move(text);
        message_is_error = error;
    }
};

// A single line of the stack view.
struct StackEntry {
    std::uint32_t address{0};
    std::uint32_t value{0};
    std::string   annotation;
};

// Formatting helpers shared by the views.
std::string hexWord(std::uint32_t value);
std::string hexAddress(std::uint32_t value);
std::string formatWallTime(double nanoseconds);
std::string formatDouble(double value, int precision = 2);

// Register alias, e.g. r14 -> sp.
std::string registerAlias(unsigned index);

// Breakpoint list maintained locally by the TUI.
bool addBreakpoint(TuiState& state, std::uint64_t address, std::string& error);
bool removeBreakpoint(TuiState& state, std::uint64_t address, std::string& error);
bool hasBreakpoint(const TuiState& state, std::uint32_t address);

// Active stack frame between FP and SP, highest address first.
// `max_words` bounds the window so the view can never overflow.
std::vector<StackEntry> stackEntries(const DebuggerTarget& target,
                                     const DebuggerSnapshot& snapshot,
                                     std::size_t max_words);

// Parses one command line and updates `state`.  Returns the next action.
Action executeCommand(TuiState& state, DebuggerTarget& target,
                      const DebuggerSnapshot& snapshot, const std::string& line);

// Single-key shortcuts.  Returns the next action.
Action handleKey(TuiState& state, char ch);

// True when the command text is another spelling of "show the breakpoint list".
bool isListBreakpointsCommand(const std::string& line);

} // namespace tui

#endif // RISC201_TUI_DEBUGGER_MODEL_HPP
