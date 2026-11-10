#include "renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "instruction_text.hpp"

namespace tui {
namespace {

const DebugField* findField(const PipelineStageSnapshot& stage, const char* name) {
    for (const DebugField& field : stage.fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

bool fieldFlag(const PipelineStageSnapshot& stage, const char* name) {
    const DebugField* field = findField(stage, name);
    return field != nullptr && field->value != 0;
}

// Compact 24-bit hex, e.g. 0x12AB for small values, full width otherwise.
std::string compactHex(std::uint64_t value) {
    char buffer[24];
    if (value <= 0xFFFFFFull)
        std::snprintf(buffer, sizeof(buffer), "0x%llX",
                      static_cast<unsigned long long>(value));
    else
        std::snprintf(buffer, sizeof(buffer), "0x%08llX",
                      static_cast<unsigned long long>(value) & 0xFFFFFFFFull);
    return buffer;
}

std::string stageDetail(const PipelineStageSnapshot& stage,
                        const std::vector<std::string>& program) {
    if (!stage.is_valid) return "bubble";

    const int rd = stageDestRegister(stage);
    std::string detail;

    if (const DebugField* result = findField(stage, "EXRESULT")) {
        detail = compactHex(result->value);
    } else if (const DebugField* readData = findField(stage, "READ_DATA")) {
        if (readData->value != 0) detail = "m" + compactHex(readData->value);
    } else if (const DebugField* op1 = findField(stage, "OP1")) {
        detail = "a" + compactHex(op1->value);
        if (const DebugField* op2 = findField(stage, "OP2"))
            detail += " b" + compactHex(op2->value);
    } else if (const DebugField* pc = findField(stage, "PC")) {
        detail = "pc " + compactHex(pc->value);
    }

    if (rd >= 0 && fieldFlag(stage, "IS_WB")) {
        if (!detail.empty()) detail += " ";
        detail += "\u2192r" + std::to_string(rd);
    }

    if (detail.empty()) {
        const std::string text = stageInstructionText(stage, program);
        detail = text.size() <= 12 ? text : text.substr(0, 12);
    }
    return detail;
}

Style stateStyle(const TuiState& state, const DebuggerSnapshot& snapshot) {
    if (snapshot.is_stopped) {
        return snapshot.stop_reason == DebugStopReason::ProgramHalted ? Style::GoodBold
                                                                     : Style::WarnBold;
    }
    if (state.running) return Style::AccentBold;
    if (state.paused_at_breakpoint) return Style::WarnBold;
    return Style::AccentBold;
}

std::string runStateText(const TuiState& state, const DebuggerSnapshot& snapshot) {
    if (snapshot.is_stopped) {
        switch (snapshot.stop_reason) {
            case DebugStopReason::ProgramHalted: return "HALTED";
            case DebugStopReason::Breakpoint:
            case DebugStopReason::AddressBreakpoint: return "BREAKPOINT";
            case DebugStopReason::CpuError: return "ERROR";
            default: return "STOPPED";
        }
    }
    if (state.running) return "RUNNING";
    if (state.paused_at_breakpoint) return "BREAKPOINT";
    return "PAUSED";
}

} // namespace

Renderer::Layout Renderer::layoutFor(int width, int height) const {
    Layout layout;

    // Very short terminals get a single reduced card row and no side panel.
    layout.compact = height < 22 || width < 72;

    layout.header_y = 0;
    layout.pipeline_y = 1;
    layout.pipeline_height = layout.compact ? 4 : 6;

    const int footer_height = 2;
    layout.footer_y = std::max(layout.pipeline_y + layout.pipeline_height + 3,
                               height - footer_height);

    layout.main_y = layout.pipeline_y + layout.pipeline_height;
    layout.main_height = std::max(0, layout.footer_y - layout.main_y);

    return layout;
}

Screen Renderer::build(const TuiState& state, const DebuggerTarget& target,
                       const DebuggerSnapshot& snapshot, const RenderContext& context,
                       int width, int height) const {
    Screen screen(width, height, context.colour);
    const Layout layout = layoutFor(width, height);

    drawHeader(screen, state, snapshot, context);
    drawPipeline(screen, state, snapshot, context, layout);
    drawMain(screen, state, target, snapshot, context, layout);
    drawFooter(screen, state, layout);

    return screen;
}

std::string Renderer::render(const TuiState& state, const DebuggerTarget& target,
                             const DebuggerSnapshot& snapshot, const RenderContext& context,
                             int width, int height) const {
    return build(state, target, snapshot, context, width, height).render();
}

void Renderer::drawHeader(Screen& screen, const TuiState& state,
                          const DebuggerSnapshot& snapshot,
                          const RenderContext& context) const {
    const int width = screen.width();
    screen.fillRow(0, 0, width, " ", Style::Header);

    int x = 1;
    auto place = [&](const std::string& text, Style style) {
        screen.text(x, 0, text, style);
        x += static_cast<int>(Screen::splitGlyphs(text).size());
    };

    place("RISC201", Style::Header);
    place(" TUI Debugger ", Style::Header);
    place("| ", Style::Dim);
    place(context.model_name, Style::Bold);
    place(" |  ", Style::Dim);

    char cycles[64];
    std::snprintf(cycles, sizeof(cycles), "cycle %llu",
                  static_cast<unsigned long long>(snapshot.total_cycles));
    place(cycles, Style::Bold);

    const std::string run_text = runStateText(state, snapshot);
    const Style run_style = stateStyle(state, snapshot);
    const int run_x = std::max(x + 2, width - static_cast<int>(run_text.size()) - 2);
    screen.text(run_x, 0, run_text, run_style);
}

void Renderer::drawPipeline(Screen& screen, const TuiState& state,
                            const DebuggerSnapshot& snapshot,
                            const RenderContext& context, const Layout& layout) const {
    const int width = screen.width();
    const int box_width = width;
    const int box_height = layout.pipeline_height;

    screen.box(0, layout.pipeline_y, box_width, box_height, Style::Dim);
    screen.boxTitle(0, layout.pipeline_y, box_width, "Pipeline", Style::AccentBold);

    const std::vector<PipelineStageSnapshot>& stages = snapshot.pipeline;
    if (stages.empty()) {
        screen.text(3, layout.pipeline_y + 1, "no pipeline state", Style::Dim);
        return;
    }

    // Cards are laid out edge to edge inside the box, one column per stage.
    const int inner_x = 2;
    const int inner_width = box_width - 4;
    const int count = static_cast<int>(stages.size());
    const int gap = 3;
    int card_width = (inner_width - gap * (count - 1)) / count;
    if (card_width < 8) {
        card_width = 8;
    }

    const int top = layout.pipeline_y + 1;
    const int card_height = box_height - 2;

    for (int i = 0; i < count; ++i) {
        const PipelineStageSnapshot& stage = stages[static_cast<std::size_t>(i)];
        const int x = inner_x + i * (card_width + gap);
        if (x + card_width > box_width - 1) break;

        const bool live = stage.is_valid;
        const Style name_style = live ? Style::AccentBold : Style::Dim;

        // Stage name with a validity dot.
        screen.put(x, top, live ? "\u25CF" : "\u25CB",
                   live ? Style::GoodBold : Style::Dim);
        screen.text(x + 2, top, stage.stage_name, name_style);

        const int content_width = card_width - 2;
        if (card_height >= 3 && content_width > 4) {
            std::string mnemonic = stageInstructionText(stage, context.program);
            screen.text(x + 2, top + 1,
                        Screen::truncate(mnemonic, content_width),
                        live ? Style::Bold : Style::Dim);

            std::string detail = stageDetail(stage, context.program);
            screen.text(x + 2, top + 2,
                        Screen::truncate(detail, content_width), Style::Dim);
        }

        // Flow arrow between cards.
        if (i + 1 < count) {
            const int arrow_x = x + card_width + (gap - 1) / 2;
            if (arrow_x < box_width - 1) {
                screen.put(arrow_x, top + 1, "\u2192", Style::Dim);
            }
        }
    }
}

void Renderer::drawMain(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                        const DebuggerSnapshot& snapshot, const RenderContext& context,
                        const Layout& layout) const {
    const int width = screen.width();
    const int top = layout.main_y;
    const int height = layout.main_height;
    if (height <= 2) return;

    int main_width = width;
    int side_width = 0;

    if (!layout.compact) {
        side_width = std::max(26, width / 3);
        if (side_width > 40) side_width = 40;
        main_width = width - side_width - 1;
    }

    switch (state.view) {
        case View::Overview: drawRegisters(screen, 0, top, main_width, height, snapshot); break;
        case View::Memory:   drawMemory(screen, state, target, 0, top, main_width, height);   break;
        case View::Stack:    drawStack(screen, state, target, snapshot, 0, top, main_width, height); break;
        case View::System:   drawSystem(screen, 0, top, main_width, height, snapshot);         break;
        case View::Stats:    drawStats(screen, state, snapshot, context, 0, top, main_width, height); break;
        case View::Breaks:   drawBreaks(screen, state, snapshot, 0, top, main_width, height);  break;
        case View::Help:     drawHelp(screen, state, 0, top, main_width, height);              break;
    }

    if (side_width > 0) {
        drawSidePanel(screen, state, snapshot, width - side_width, top, side_width, height);
    }
}

void Renderer::drawRegisters(Screen& screen, int x, int y, int width, int height,
                             const DebuggerSnapshot& snapshot) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Register File", Style::AccentBold);

    if (height < 4 || width < 12) return;

    constexpr int kEntryWidth = 20;   // "r12/zero  0x00000000"
    constexpr int kTotal = 16;
    int columns = (width - 4) / kEntryWidth;
    columns = std::max(1, std::min(3, columns));
    const int rows = (kTotal + columns - 1) / columns;

    for (int index = 0; index < kTotal; ++index) {
        const int row = index % rows;
        const int column = index / rows;
        if (column >= columns) break;
        if (row + 1 > height - 2) break;

        const int cx = x + 2 + column * kEntryWidth;
        const int cy = y + 1 + row;

        const unsigned value = snapshot.registers[static_cast<std::size_t>(index)];
        const std::string alias = registerAlias(static_cast<unsigned>(index));

        Style style = value == 0 ? Style::Dim : Style::Bold;
        if (index == 12) style = Style::Dim;

        std::string label = "r" + std::to_string(index);
        if (!alias.empty()) label += "/" + alias;

        screen.text(cx, cy, Screen::padRight(label, 9), Style::Accent);
        screen.text(cx + 9, cy, hexWord(value), style);
    }
}

void Renderer::drawMemory(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                          int x, int y, int width, int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Memory @ " + hexAddress(state.memory_base),
                    Style::AccentBold);

    if (height < 5 || width < 20) return;

    const int rows = height - 3;
    const int columns = std::max(1, std::min(4, (width - 13) / 11));

    // Column-offset header, aligned with the values drawn below.
    for (int column = 0; column < columns; ++column) {
        const int cx = x + 13 + column * 11;
        if (cx + 10 > x + width - 1) break;
        screen.text(cx, y + 1, "+" + std::to_string(column * 4), Style::Dim);
    }

    for (int row = 0; row < rows; ++row) {
        const int cy = y + 2 + row;
        if (cy >= y + height - 1) break;

        const std::uint32_t base =
            state.memory_base + static_cast<std::uint32_t>(row * columns * 4);
        screen.text(x + 2, cy, hexAddress(base), Style::Accent);

        for (int column = 0; column < columns; ++column) {
            const int cx = x + 13 + column * 11;
            if (cx + 10 > x + width - 1) break;
            std::uint32_t value = 0;
            target.readMemoryWord(base + static_cast<std::uint32_t>(column * 4), value);
            screen.text(cx, cy, Screen::padRight(compactHex(value), 10),
                        value == 0 ? Style::Dim : Style::Normal);
        }
    }
}

void Renderer::drawStack(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                         const DebuggerSnapshot& snapshot, int x, int y, int width,
                         int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Active Stack Frame", Style::AccentBold);

    if (height < 4) return;

    const std::uint32_t sp = snapshot.registers[14];
    const std::uint32_t fp = snapshot.registers[13];

    screen.text(x + 2, y + 1, "SP = " + hexWord(sp), Style::Accent);
    screen.text(x + 22, y + 1, "FP = " + hexWord(fp), Style::Accent);

    if (sp == 0 && fp == 0) {
        screen.text(x + 2, y + 3, "Stack is uninitialized (SP = FP = 0).", Style::Warn);
        screen.text(x + 2, y + 4, "Use 'mem <addr>' to browse memory directly.", Style::Dim);
        return;
    }

    const std::size_t max_words = static_cast<std::size_t>(std::max(1, height - 5));
    std::size_t requested = max_words;
    if (state.stack_extra_words > 0)
        requested = std::min<std::size_t>(max_words, state.stack_extra_words);
    const std::vector<StackEntry> entries = stackEntries(target, snapshot, requested);

    screen.text(x + 2, y + 2, Screen::padRight("address", 12) +
                                      Screen::padRight("value", 13) + "frame",
                Style::Dim);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const int cy = y + 3 + static_cast<int>(i);
        if (cy >= y + height - 1) break;

        const StackEntry& entry = entries[i];
        screen.text(x + 2, cy, Screen::padRight(hexAddress(entry.address), 12), Style::Accent);
        screen.text(x + 14, cy, Screen::padRight(hexWord(entry.value), 13),
                    entry.value == 0 ? Style::Dim : Style::Normal);
        if (!entry.annotation.empty()) {
            screen.text(x + 27, cy, "<- " + entry.annotation, Style::WarnBold);
        }
    }
}

void Renderer::drawSystem(Screen& screen, int x, int y, int width, int height,
                          const DebuggerSnapshot& snapshot) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "System Registers", Style::AccentBold);

    int cy = y + 1;
    auto row = [&](const std::string& label, const std::string& value,
                   Style style = Style::Normal) {
        if (cy >= y + height - 1) return;
        screen.text(x + 2, cy, Screen::padRight(label, 12), Style::Dim);
        screen.text(x + 15, cy, value, style);
        ++cy;
    };

    row("PC", hexWord(snapshot.pc), Style::Bold);
    row("EPC", snapshot.has_epc ? hexWord(snapshot.epc) : "n/a");
    row("cause", snapshot.has_cause ? std::to_string(snapshot.cause) : "n/a");
    row("flags.E", snapshot.has_flags ? (snapshot.flag_e ? "1" : "0") : "n/a");
    row("flags.GT", snapshot.has_flags ? (snapshot.flag_gt ? "1" : "0") : "n/a");
    row("status", snapshot.is_stopped ? "stopped" : "running",
        snapshot.is_stopped ? Style::WarnBold : Style::GoodBold);
    row("stop", stopReasonName(snapshot.stop_reason));
    row("microPC", snapshot.has_micro_pc ? hexWord(snapshot.micro_pc) : "n/a");
}

void Renderer::drawStats(Screen& screen, const TuiState& state,
                         const DebuggerSnapshot& snapshot,
                         const RenderContext& context, int x, int y, int width,
                         int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Execution Statistics", Style::AccentBold);

    if (!snapshot.has_stats) {
        screen.text(x + 2, y + 1, "statistics unavailable for this target", Style::Dim);
        return;
    }

    int cy = y + 1;
    auto row = [&](const std::string& label, const std::string& value,
                   Style style = Style::Normal) {
        if (cy >= y + height - 1) return;
        screen.text(x + 2, cy, Screen::padRight(label, 22), Style::Dim);
        screen.text(x + 25, cy, value, style);
        ++cy;
    };

    row("total cycles", std::to_string(snapshot.total_cycles), Style::Bold);
    row("stall cycles", std::to_string(snapshot.stall_cycles), Style::Warn);
    row("instructions retired", std::to_string(snapshot.instructions_retired));

    const double cpi = snapshot.instructions_retired > 0
        ? static_cast<double>(snapshot.total_cycles) /
          static_cast<double>(snapshot.instructions_retired)
        : 0.0;
    row("CPI", formatDouble(cpi));

    const double stall_pct = snapshot.total_cycles > 0
        ? (static_cast<double>(snapshot.stall_cycles) /
           static_cast<double>(snapshot.total_cycles)) * 100.0
        : 0.0;
    row("stall overhead", formatDouble(stall_pct, 1) + "%",
        stall_pct > 25.0 ? Style::WarnBold : Style::Normal);

    if (context.clock_period_ns > 0.0) {
        row("clock period", formatDouble(context.clock_period_ns, 2) + " ns");
        row("simulated time", formatWallTime(static_cast<double>(snapshot.total_cycles) *
                                             context.clock_period_ns), Style::Bold);
        row("session time", formatWallTime(state.session_ms * 1.0e6));
    }
}

void Renderer::drawBreaks(Screen& screen, const TuiState& state,
                          const DebuggerSnapshot& snapshot, int x, int y, int width,
                          int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Breakpoints", Style::AccentBold);

    if (height < 4) return;

    if (state.breakpoints.empty()) {
        screen.text(x + 2, y + 1, "no breakpoints", Style::Dim);
        screen.text(x + 2, y + 2, "add one with: break <addr>", Style::Dim);
        return;
    }

    const int rows = height - 2;
    for (int i = 0; i < rows; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        if (index >= state.breakpoints.size()) break;
        const int cy = y + 1 + i;
        const std::uint32_t address = state.breakpoints[index];
        const bool here = address == snapshot.pc;

        screen.text(x + 2, cy, Screen::padRight(hexAddress(address), 14),
                    here ? Style::WarnBold : Style::Accent);
        if (here) screen.text(x + 17, cy, "<- PC", Style::WarnBold);
    }
}

void Renderer::drawHelp(Screen& screen, const TuiState& state,
                        int x, int y, int width, int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "Commands", Style::AccentBold);

    static const char* lines[] = {
        "views   1/g regs   2/m memory   3/k stack   4/y system",
        "        5/t stats   6/b breaks   7/h help   Tab next view",
        "exec    s or space step 1     r run to breakpoint",
        "        f finish (run to halt) R reset program",
        "command : opens a command line; any of the following:",
        "step [n]        advance n clock cycles",
        "run             run until a breakpoint or halt",
        "finish          run to completion, ignoring breaks",
        "reset           reload the program",
        "break <addr>    add a breakpoint",
        "delete <addr>   remove a breakpoint",
        "breaks          show the breakpoint list",
        "regs            register file view",
        "mem [addr]      memory window at addr (default: SP)",
        "stack [n]       active stack frame",
        "sysregs         system registers",
        "stats           cycle and stall statistics",
        "help / ?        this list",
        "quit            leave the TUI",
    };

    const int rows = height - 2;
    const int count = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
    const int max_scroll = std::max(0, count - rows);
    const int offset = std::max(0, std::min(state.scroll, max_scroll));

    for (int i = 0; i < rows; ++i) {
        const int index = offset + i;
        if (index >= count) break;
        screen.text(x + 2, y + 1 + i, Screen::truncate(lines[index], width - 4),
                    Style::Normal);
    }
    if (max_scroll > 0) {
        screen.text(x + width - 15, y + height - 1,
                    "scroll " + std::to_string(offset) + "/" + std::to_string(max_scroll),
                    Style::Dim);
    }
}

void Renderer::drawSidePanel(Screen& screen, const TuiState& state,
                             const DebuggerSnapshot& snapshot, int x, int y,
                             int width, int height) const {
    screen.box(x, y, width, height, Style::Dim);
    screen.boxTitle(x, y, width, "State", Style::AccentBold);

    int cy = y + 1;
    auto row = [&](const std::string& label, const std::string& value,
                   Style style = Style::Normal) {
        if (cy >= y + height - 1) return;
        screen.text(x + 2, cy, Screen::padRight(label, 9), Style::Dim);
        screen.text(x + 12, cy, Screen::truncate(value, width - 14), style);
        ++cy;
    };

    row("PC", hexWord(snapshot.pc), Style::Bold);
    row("retired", std::to_string(snapshot.instructions_retired));
    row("stalls", std::to_string(snapshot.stall_cycles));
    row("E", snapshot.has_flags && snapshot.flag_e ? "1" : "0",
        snapshot.has_flags && snapshot.flag_e ? Style::GoodBold : Style::Dim);
    row("GT", snapshot.has_flags && snapshot.flag_gt ? "1" : "0",
        snapshot.has_flags && snapshot.flag_gt ? Style::GoodBold : Style::Dim);
    row("epc", snapshot.has_epc ? hexWord(snapshot.epc) : "n/a");
    row("cause", snapshot.has_cause ? std::to_string(snapshot.cause) : "n/a");

    if (cy < y + height - 1) {
        ++cy;
        screen.text(x + 2, cy, Screen::truncate("breakpoints", width - 4), Style::Dim);
        ++cy;
        if (state.breakpoints.empty()) {
            if (cy < y + height - 1)
                screen.text(x + 2, cy, "none", Style::Dim);
        } else {
            for (std::size_t i = 0; i < state.breakpoints.size() &&
                                    cy < y + height - 1; ++i) {
                screen.text(x + 2, cy,
                            Screen::truncate(hexAddress(state.breakpoints[i]), width - 4),
                            Style::Magenta);
                ++cy;
            }
        }
    }

    if (cy < y + height - 1) {
        ++cy;
        screen.text(x + 2, cy, Screen::truncate("view: " + std::string(viewName(state.view)),
                                                width - 4), Style::AccentBold);
    }
}

void Renderer::drawFooter(Screen& screen, const TuiState& state,
                          const Layout& layout) const {
    const int width = screen.width();
    const int message_y = layout.footer_y;
    const int hint_y = layout.footer_y + 1;

    if (state.command_active) {
        screen.fillRow(0, message_y, width, " ", Style::Normal);
        const std::string prompt = ":" + state.command_buffer + "_";
        screen.text(1, message_y, Screen::truncate(prompt, width - 2), Style::Bold);
    } else if (!state.message.empty()) {
        screen.fillRow(0, message_y, width, " ", Style::Normal);
        screen.text(1, message_y, Screen::truncate(state.message, width - 2),
                    state.message_is_error ? Style::BadBold : Style::Good);
    } else {
        screen.fillRow(0, message_y, width, " ", Style::Normal);
        screen.text(1, message_y, "ready", Style::Dim);
    }

    if (hint_y < screen.height()) {
        screen.fillRow(0, hint_y, width, " ", Style::Normal);
        const char* hints =
            " s step  r run  f finish  R reset  m mem  k stack  y sys  t stats  "
            "b breaks  g regs  h help  : cmd  q quit";
        screen.text(1, hint_y, Screen::truncate(hints, width - 2), Style::Dim);
    }
}

} // namespace tui
