#ifndef RISC201_TUI_RENDERER_HPP
#define RISC201_TUI_RENDERER_HPP

// ---------------------------------------------------------------------------
// Draws one TUI frame from a snapshot + the TUI state.
//
// Layout is intentionally three horizontal bands, not a mosaic of panels:
//
//   +--------------------------------------------------------------+
//   | header: model, cycle, run state                              |
//   +--------------------------------------------------------------+
//   | pipeline strip: one card per stage, in order                 |
//   +-------------------------------+------------------------------+
//   | primary view (registers /     | side panel: flags, CSRs,     |
//   | memory / stack / ...)         | breakpoints, stop reason     |
//   +-------------------------------+------------------------------+
//   | footer: message + key hints                                  |
//   +--------------------------------------------------------------+
//
// The side panel is the only always-on inspector, which is what keeps the
// screen calm while still exposing everything the CLI debugger can show.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "debugger.hpp"
#include "debugger_model.hpp"
#include "screen.hpp"

namespace tui {

struct RenderContext {
    std::string model_name{"6-stage"};
    double      clock_period_ns{0.0};
    std::vector<std::string> program;   // raw loaded program image
    std::vector<std::uint32_t> words;   // parsed program image
    bool        colour{true};
};

class Renderer {
public:
    // Builds the frame for the current size.  Returns the ANSI text.
    std::string render(const TuiState& state, const DebuggerTarget& target,
                       const DebuggerSnapshot& snapshot, const RenderContext& context,
                       int width, int height) const;

    // Same drawing without serialising, so tests can inspect rows directly.
    Screen build(const TuiState& state, const DebuggerTarget& target,
                 const DebuggerSnapshot& snapshot, const RenderContext& context,
                 int width, int height) const;

    // Exposed for tests: the layout rectangle maths.
    struct Layout {
        int header_y{0};
        int pipeline_y{0};
        int pipeline_height{0};
        int main_y{0};
        int main_height{0};
        int footer_y{0};
        bool compact{false};
    };

    Layout layoutFor(int width, int height) const;

private:
    void drawHeader(Screen& screen, const TuiState& state,
                    const DebuggerSnapshot& snapshot, const RenderContext& context) const;

    void drawPipeline(Screen& screen, const TuiState& state,
                      const DebuggerSnapshot& snapshot,
                      const RenderContext& context, const Layout& layout) const;

    void drawMain(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                  const DebuggerSnapshot& snapshot, const RenderContext& context,
                  const Layout& layout) const;

    void drawFooter(Screen& screen, const TuiState& state, const Layout& layout) const;

    // Individual primary views, drawn into a rectangle.
    void drawRegisters(Screen& screen, int x, int y, int width, int height,
                       const DebuggerSnapshot& snapshot) const;
    void drawMemory(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                    int x, int y, int width, int height) const;
    void drawStack(Screen& screen, const TuiState& state, const DebuggerTarget& target,
                   const DebuggerSnapshot& snapshot, int x, int y, int width, int height) const;
    void drawSystem(Screen& screen, int x, int y, int width, int height,
                    const DebuggerSnapshot& snapshot) const;
    void drawStats(Screen& screen, const TuiState& state, const DebuggerSnapshot& snapshot,
                   const RenderContext& context, int x, int y, int width, int height) const;
    void drawBreaks(Screen& screen, const TuiState& state, const DebuggerSnapshot& snapshot,
                    int x, int y, int width, int height) const;
    void drawHelp(Screen& screen, const TuiState& state,
                  int x, int y, int width, int height) const;

    void drawSidePanel(Screen& screen, const TuiState& state,
                       const DebuggerSnapshot& snapshot, int x, int y,
                       int width, int height) const;
};

} // namespace tui

#endif // RISC201_TUI_RENDERER_HPP
