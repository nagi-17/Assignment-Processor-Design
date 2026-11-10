#ifndef RISC201_TUI_SCREEN_HPP
#define RISC201_TUI_SCREEN_HPP

// ---------------------------------------------------------------------------
// Small cell-based canvas used by the TUI debugger.
//
// The canvas is deliberately independent of the debugger model: it only knows
// how to place UTF-8 glyphs, draw light box borders and colour them, then
// serialise the result to one ANSI frame.  That keeps layout code testable
// without a terminal.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

namespace tui {

enum class Style {
    Normal,
    Dim,
    Bold,
    Accent,
    AccentBold,
    Good,
    GoodBold,
    Warn,
    WarnBold,
    Bad,
    BadBold,
    Magenta,
    MagentaBold,
    Header
};

class Screen {
public:
    Screen(int width, int height, bool colour = true);

    int width() const  { return width_; }
    int height() const { return height_; }

    void clear();

    // Places one glyph.  Out-of-range coordinates are ignored.
    void put(int x, int y, const std::string& glyph, Style style = Style::Normal);

    // Places a UTF-8 string starting at (x, y), clipping at the right edge and
    // at embedded newlines.
    void text(int x, int y, const std::string& value, Style style = Style::Normal);

    void fillRow(int x, int y, int width, const std::string& glyph,
                 Style style = Style::Normal);
    void hline(int x, int y, int width, Style style = Style::Dim);
    void vline(int x, int y, int height, Style style = Style::Dim);

    // Box-drawn frame.  border_style paints the edges; interior is untouched.
    void box(int x, int y, int width, int height, Style border_style = Style::Dim);

    // Writes a title over the top border, starting at x + 2.
    void boxTitle(int x, int y, int width, const std::string& title,
                  Style style = Style::Bold);

    // Serialises the canvas as one frame, each row positioned absolutely so a
    // frame never scrolls the terminal.
    std::string render() const;

    // Visible text of one row, used by the renderer tests.
    std::string rowText(int y) const;

    static std::vector<std::string> splitGlyphs(const std::string& text);
    static std::string truncate(const std::string& text, int max_glyphs);
    static std::string padRight(const std::string& text, int width);
    static std::string padLeft(const std::string& text, int width);

private:
    struct Cell {
        std::string glyph{" "};
        Style style{Style::Normal};
    };

    Cell& at(int x, int y);
    const Cell& at(int x, int y) const;

    int width_;
    int height_;
    bool colour_;
    std::vector<Cell> cells_;
};

const char* styleEscape(Style style);

} // namespace tui

#endif // RISC201_TUI_SCREEN_HPP
