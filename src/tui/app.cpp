#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

#include "instruction_text.hpp"
#include "renderer.hpp"
#include "terminal.hpp"

namespace tui {
namespace {

// Cycles executed between two frames while fast-forwarding.  Small enough to
// feel live, large enough that a long program does not crawl.
constexpr std::uint64_t kRunChunk = 400;

RenderContext makeContext(const TuiOptions& options, bool colour) {
    RenderContext context;
    context.model_name = options.model_name;
    context.clock_period_ns = options.clock_period_ns;
    context.program = options.program;
    context.words = parseProgramImage(options.program);
    context.colour = colour;
    return context;
}

// Compares the PC against the locally maintained breakpoint list.  The TUI
// owns this bookkeeping because the CPU classes only poll their own internal
// (unused) list.
void observeBreakpoint(TuiState& state, const DebuggerSnapshot& snapshot,
                       bool ignore_user_breakpoints) {
    if (snapshot.is_stopped) {
        state.halted = true;
        state.running = false;
        state.ignore_breakpoints = false;
        return;
    }
    if (!ignore_user_breakpoints && hasBreakpoint(state, snapshot.pc)) {
        state.paused_at_breakpoint = true;
        state.paused_address = snapshot.pc;
        state.running = false;
        state.ignore_breakpoints = false;
        state.setMessage("breakpoint hit at " + hexAddress(snapshot.pc));
    }
}

std::uint64_t stepTarget(DebuggerTarget& cpu, TuiState& state, std::uint64_t count) {
    std::uint64_t done = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (!cpu.stepCycle()) break;
        ++done;
        const DebuggerSnapshot snapshot = cpu.getSnapshot();
        observeBreakpoint(state, snapshot, false);
        if (state.paused_at_breakpoint || snapshot.is_stopped) break;
    }
    return done;
}

Action applyAction(Action action, TuiTarget& cpu, TuiState& state,
                   const DebuggerSnapshot& snapshot) {
    switch (action.kind) {
        case ActionKind::Step:
            state.paused_at_breakpoint = false;
            state.ignore_breakpoints = false;
            stepTarget(cpu, state, action.count);
            break;
        case ActionKind::Continue:
            if (snapshot.is_stopped) {
                state.setMessage("CPU is halted; press R to reset", true);
                break;
            }
            state.paused_at_breakpoint = false;
            state.ignore_breakpoints = false;
            state.running = true;
            break;
        case ActionKind::RunToEnd:
            if (snapshot.is_stopped) {
                state.setMessage("CPU is halted; press R to reset", true);
                break;
            }
            state.paused_at_breakpoint = false;
            state.ignore_breakpoints = true;   // keep the breakpoint list
            state.running = true;
            break;
        case ActionKind::Reload:
            cpu.reload();
            state.paused_at_breakpoint = false;
            state.ignore_breakpoints = false;
            state.halted = false;
            state.running = false;
            state.setMessage("program reloaded");
            break;
        default:
            break;
    }
    return action;
}

} // namespace

std::string renderOnce(DebuggerTarget& cpu, const TuiOptions& options,
                       const TuiState& state, int width, int height) {
    Renderer renderer;
    const DebuggerSnapshot snapshot = cpu.getSnapshot();
    const bool colour = !options.force_no_colour && std::getenv("NO_COLOR") == nullptr;
    return renderer.render(state, cpu, snapshot, makeContext(options, colour),
                           width, height);
}

bool runTuiDebugger(TuiTarget& cpu, const TuiOptions& options) {
    Terminal terminal;
    if (!terminal.enter()) return false;

    TuiState state;
    Renderer renderer;
    const bool colour = !options.force_no_colour && std::getenv("NO_COLOR") == nullptr;
    const RenderContext context = makeContext(options, colour);

    bool running = true;
    bool dirty = true;
    const auto session_start = std::chrono::steady_clock::now();

    while (running) {
        const DebuggerSnapshot snapshot = cpu.getSnapshot();
        state.session_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - session_start).count();

        if (dirty) {
            const Size size = terminal.size();
            terminal.present(renderer.render(state, cpu, snapshot, context,
                                             size.width, size.height));
            dirty = false;
        }

        // ---- fast-forward ----
        if (state.running && !snapshot.is_stopped) {
            for (std::uint64_t i = 0; i < kRunChunk && state.running; ++i) {
                if (!cpu.stepCycle()) {
                    state.running = false;
                    state.halted = true;
                    break;
                }
                observeBreakpoint(state, cpu.getSnapshot(), state.ignore_breakpoints);
            }

            // Let the user interrupt a long run without waiting for a frame.
            Key key;
            if (terminal.readKey(key, 0)) {
                if (key.type == KeyType::CtrlC || key.is('q') || key.type == KeyType::Escape) {
                    state.running = false;
                    state.ignore_breakpoints = false;
                    state.setMessage("run interrupted");
                }
            }
            dirty = true;
            continue;
        }

        // ---- paused: wait for a key ----
        Key key;
        if (!terminal.readKey(key, 250)) {
            // Timeout only exists so a terminal resize redraws the frame.
            dirty = true;
            continue;
        }

        if (state.command_active) {
            if (key.type == KeyType::Enter) {
                state.command_active = false;
                const std::string line = state.command_buffer;
                state.command_buffer.clear();
                const DebuggerSnapshot current = cpu.getSnapshot();
                const Action action = executeCommand(state, cpu, current, line);
                if (action.kind == ActionKind::Quit) { running = false; break; }
                applyAction(action, cpu, state, current);
                dirty = true;
            } else if (key.type == KeyType::Escape) {
                state.command_active = false;
                state.command_buffer.clear();
                state.setMessage("cancelled");
                dirty = true;
            } else if (key.type == KeyType::Backspace) {
                if (!state.command_buffer.empty()) state.command_buffer.pop_back();
                dirty = true;
            } else if (key.type == KeyType::Char &&
                       state.command_buffer.size() < 120) {
                state.command_buffer.push_back(key.ch);
                dirty = true;
            }
            continue;
        }

        if (key.type == KeyType::CtrlC) { running = false; break; }

        if (key.type == KeyType::Char && (key.ch == ':' || key.ch == '/')) {
            state.command_active = true;
            state.command_buffer.clear();
            dirty = true;
            continue;
        }

        if (key.type == KeyType::Tab) {
            state.view = static_cast<View>((static_cast<int>(state.view) + 1) % 7);
            state.scroll = 0;
            dirty = true;
            continue;
        }

        if (key.type == KeyType::Char) {
            bool handled = true;
            View next = state.view;
            switch (key.ch) {
                case '1': case 'g': next = View::Overview; break;
                case '2': case 'm': next = View::Memory;   break;
                case '3': case 'k': next = View::Stack;    break;
                case '4': case 'y': next = View::System;   break;
                case '5': case 't': next = View::Stats;    break;
                case '6': case 'b': next = View::Breaks;   break;
                case '7': case 'h': case '?': next = View::Help; break;
                default:  handled = false;                 break;
            }
            if (handled) {
                state.view = next;
                state.scroll = 0;
                if (next == View::Memory && state.memory_base == 0 &&
                    snapshot.registers[14] != 0) {
                    state.memory_base = snapshot.registers[14] & ~3u;
                }
                dirty = true;
                continue;
            }
        }

        if (state.view == View::Memory) {
            if (key.type == KeyType::Down) {
                state.memory_base += 4;
                dirty = true;
                continue;
            }
            if (key.type == KeyType::Up) {
                state.memory_base = state.memory_base >= 4 ? state.memory_base - 4 : 0;
                dirty = true;
                continue;
            }
            if (key.type == KeyType::PageDown) {
                state.memory_base += 40;
                dirty = true;
                continue;
            }
            if (key.type == KeyType::PageUp) {
                state.memory_base = state.memory_base > 40 ? state.memory_base - 40 : 0;
                dirty = true;
                continue;
            }
        }

        if (state.view == View::Help) {
            if (key.type == KeyType::Down) {
                state.scroll += 1;
                dirty = true;
                continue;
            }
            if (key.type == KeyType::Up) {
                state.scroll = std::max(0, state.scroll - 1);
                dirty = true;
                continue;
            }
            if (key.type == KeyType::PageDown) {
                state.scroll += 10;
                dirty = true;
                continue;
            }
            if (key.type == KeyType::PageUp) {
                state.scroll = std::max(0, state.scroll - 10);
                dirty = true;
                continue;
            }
        }

        if (key.type != KeyType::Char) continue;

        const Action action = handleKey(state, key.ch);
        if (action.kind == ActionKind::Quit) { running = false; break; }
        applyAction(action, cpu, state, snapshot);
        dirty = true;
    }

    terminal.leave();
    return true;
}

} // namespace tui
