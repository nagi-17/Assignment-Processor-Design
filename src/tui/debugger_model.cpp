#include "debugger_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace tui {
namespace {

std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() &&
           std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    std::size_t last = text.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::vector<std::string> splitWords(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

constexpr std::uint32_t kStackWindowDefault = 8;
constexpr std::uint32_t kStackWindowMax = 32;

} // namespace

const char* viewName(View view) {
    switch (view) {
        case View::Overview: return "registers";
        case View::Memory:   return "memory";
        case View::Stack:    return "stack";
        case View::System:   return "system";
        case View::Stats:    return "stats";
        case View::Breaks:   return "breaks";
        case View::Help:     return "help";
    }
    return "?";
}

bool parseViewName(const std::string& text, View& view) {
    std::string name = text;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (name == "registers" || name == "regs" || name == "reg" || name == "overview") {
        view = View::Overview; return true;
    }
    if (name == "memory" || name == "mem") { view = View::Memory; return true; }
    if (name == "stack" || name == "frames") { view = View::Stack; return true; }
    if (name == "system" || name == "sysregs" || name == "csr") {
        view = View::System; return true;
    }
    if (name == "stats" || name == "statistics") { view = View::Stats; return true; }
    if (name == "breaks" || name == "breakpoints") { view = View::Breaks; return true; }
    if (name == "help") { view = View::Help; return true; }
    return false;
}

std::string hexWord(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return buffer;
}

std::string hexAddress(std::uint32_t value) { return hexWord(value); }

std::string formatWallTime(double nanoseconds) {
    char buffer[32];
    if (nanoseconds >= 1.0e6)      std::snprintf(buffer, sizeof(buffer), "%.2f ms", nanoseconds / 1.0e6);
    else if (nanoseconds >= 1.0e3) std::snprintf(buffer, sizeof(buffer), "%.2f us", nanoseconds / 1.0e3);
    else                           std::snprintf(buffer, sizeof(buffer), "%.2f ns", nanoseconds);
    return buffer;
}

std::string formatDouble(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string registerAlias(unsigned index) {
    switch (index) {
        case 12: return "zero";
        case 13: return "fp";
        case 14: return "sp";
        case 15: return "ra";
        default: return {};
    }
}

bool hasBreakpoint(const TuiState& state, std::uint32_t address) {
    return std::find(state.breakpoints.begin(), state.breakpoints.end(), address) !=
           state.breakpoints.end();
}

bool addBreakpoint(TuiState& state, std::uint64_t address, std::string& error) {
    if (address > 0xFFFFFFFFull) {
        error = "address is outside the 32-bit address space";
        return false;
    }
    if (address % 4 != 0) {
        error = "breakpoint address must be 4-byte aligned";
        return false;
    }
    const std::uint32_t target = static_cast<std::uint32_t>(address);
    if (hasBreakpoint(state, target)) {
        error = "breakpoint already set at " + hexAddress(target);
        return false;
    }
    state.breakpoints.push_back(target);
    std::sort(state.breakpoints.begin(), state.breakpoints.end());
    return true;
}

bool removeBreakpoint(TuiState& state, std::uint64_t address, std::string& error) {
    if (address > 0xFFFFFFFFull || address % 4 != 0) {
        error = "usage: delete <aligned address>";
        return false;
    }
    const std::uint32_t target = static_cast<std::uint32_t>(address);
    auto it = std::find(state.breakpoints.begin(), state.breakpoints.end(), target);
    if (it == state.breakpoints.end()) {
        error = "no breakpoint at " + hexAddress(target);
        return false;
    }
    state.breakpoints.erase(it);
    return true;
}

std::vector<StackEntry> stackEntries(const DebuggerTarget& target,
                                     const DebuggerSnapshot& snapshot,
                                     std::size_t max_words) {
    std::vector<StackEntry> entries;
    if (max_words == 0) return entries;

    const std::uint32_t sp = snapshot.registers[14];
    const std::uint32_t fp = snapshot.registers[13];

    // push expands to "sub sp, sp, 4" followed by a store, so the live frame
    // sits *below* SP unless the frame pointer has been grown upwards.
    const bool ascending = (fp > sp) || (sp == 0);

    std::uint32_t words = kStackWindowDefault;
    if (ascending && fp > sp && ((fp - sp) / 4) + 1 <= kStackWindowMax) {
        words = ((fp - sp) / 4) + 1;
    }
    if (words > max_words) words = static_cast<std::uint32_t>(max_words);

    entries.reserve(words);
    for (std::uint32_t i = 0; i < words; ++i) {
        StackEntry entry;
        entry.address = ascending ? sp + i * 4 : sp - i * 4;
        std::uint32_t value = 0;
        target.readMemoryWord(entry.address, value);
        entry.value = value;

        if (entry.address == sp && entry.address == fp)
            entry.annotation = "SP + FP";
        else if (entry.address == sp)
            entry.annotation = "SP";
        else if (entry.address == fp)
            entry.annotation = "FP";
        entries.push_back(entry);
    }

    // Highest address first, so the frame reads the way it is laid out on the
    // page rather than as an ascending hex dump.
    std::reverse(entries.begin(), entries.end());
    return entries;
}

bool isListBreakpointsCommand(const std::string& line) {
    const std::vector<std::string> words = splitWords(line);
    if (words.empty()) return false;
    return words[0] == "breaks" || words[0] == "breakpoints" || words[0] == "bl";
}

Action executeCommand(TuiState& state, DebuggerTarget& target,
                      const DebuggerSnapshot& snapshot, const std::string& rawLine) {
    const std::string line = trim(rawLine);
    if (line.empty()) return Action{ActionKind::None, 1};

    const std::vector<std::string> words = splitWords(line);
    const std::string& command = words[0];

    Action action{ActionKind::None, 1};

    if (command == "quit" || command == "q" || command == "exit") {
        action.kind = ActionKind::Quit;
        return action;
    }

    if (command == "help" || command == "h" || command == "?") {
        state.view = View::Help;
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "regs" || command == "registers" || command == "reg") {
        state.view = View::Overview;
        state.scroll = 0;
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "mem" || command == "memory" || command == "x") {
        state.view = View::Memory;
        state.scroll = 0;
        if (words.size() >= 2) {
            std::uint64_t address = 0;
            if (!parseNumber(words[1], address) || address > 0xFFFFFFFFull) {
                state.setMessage("usage: mem <address>", true);
            } else {
                state.memory_base = static_cast<std::uint32_t>(address) & ~3u;
                state.setMessage("memory base " + hexAddress(state.memory_base));
            }
        } else if (snapshot.registers[14] != 0) {
            // No address given: start at the live stack pointer, which is where
            // the interesting data almost always is.
            state.memory_base = snapshot.registers[14] & ~3u;
            state.setMessage("memory at SP " + hexAddress(state.memory_base));
        }
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "stack" || command == "frames") {
        state.view = View::Stack;
        if (words.size() >= 2) {
            std::uint64_t count = 0;
            if (!parseNumber(words[1], count) || count == 0 || count > kStackWindowMax) {
                state.setMessage("usage: stack [1-" + std::to_string(kStackWindowMax) + "]", true);
            } else {
                state.stack_extra_words = static_cast<std::uint32_t>(count);
            }
        }
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "sysregs" || command == "system" || command == "csr") {
        state.view = View::System;
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "stats" || command == "statistics") {
        state.view = View::Stats;
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (isListBreakpointsCommand(line)) {
        state.view = View::Breaks;
        action.kind = ActionKind::Redraw;
        return action;
    }

    if (command == "step" || command == "s") {
        std::uint64_t count = 1;
        if (words.size() >= 2) {
            if (!parseNumber(words[1], count) || count == 0 || count > 100000) {
                state.setMessage("usage: step [count] (1-100000)", true);
                return action;
            }
        }
        action.kind = ActionKind::Step;
        action.count = count;
        return action;
    }

    if (command == "run" || command == "continue" || command == "c" || command == "go") {
        action.kind = ActionKind::Continue;
        return action;
    }

    if (command == "finish" || command == "end" || command == "until-end") {
        action.kind = ActionKind::RunToEnd;
        return action;
    }

    if (command == "reset" || command == "reload") {
        action.kind = ActionKind::Reload;
        return action;
    }

    if (command == "break" || command == "b") {
        std::uint64_t address = 0;
        if (words.size() < 2 || !parseNumber(words[1], address)) {
            state.setMessage("usage: break <address>", true);
            return action;
        }
        std::string error;
        if (!addBreakpoint(state, address, error)) {
            state.setMessage(error, true);
        } else {
            state.setMessage("breakpoint added at " +
                             hexAddress(static_cast<std::uint32_t>(address)));
        }
        return action;
    }

    if (command == "delete" || command == "del" || command == "clear") {
        std::uint64_t address = 0;
        if (words.size() < 2 || !parseNumber(words[1], address)) {
            state.setMessage("usage: delete <address>", true);
            return action;
        }
        std::string error;
        if (!removeBreakpoint(state, address, error)) {
            state.setMessage(error, true);
        } else {
            state.setMessage("breakpoint removed from " +
                             hexAddress(static_cast<std::uint32_t>(address)));
        }
        return action;
    }

    (void)target;
    (void)snapshot;
    state.setMessage("unknown command '" + command + "' - press ? for help", true);
    return action;
}

Action handleKey(TuiState& state, char ch) {
    (void)state;
    Action action{ActionKind::None, 1};

    switch (ch) {
        case 'q': action.kind = ActionKind::Quit;    break;
        case ' ': action.kind = ActionKind::Step;    break;
        case 's': action.kind = ActionKind::Step;    break;
        case 'r': action.kind = ActionKind::Continue;break;
        case 'f': action.kind = ActionKind::RunToEnd;break;
        case 'R': action.kind = ActionKind::Reload;  break;
        default:  action.kind = ActionKind::None;    break;
    }
    return action;
}

} // namespace tui
