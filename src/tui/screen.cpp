#include "screen.hpp"

#include <algorithm>

namespace tui {
namespace {

// Returns the byte length of the UTF-8 sequence starting at index i.
std::size_t glyphLength(const std::string& text, std::size_t i) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t len = 1;
    if ((lead & 0x80u) == 0x00u)      len = 1;
    else if ((lead & 0xE0u) == 0xC0u) len = 2;
    else if ((lead & 0xF0u) == 0xE0u) len = 3;
    else if ((lead & 0xF8u) == 0xF0u) len = 4;
    if (i + len > text.size()) len = 1;
    return len;
}

} // namespace

Screen::Screen(int width, int height, bool colour)
    : width_(width > 0 ? width : 1),
      height_(height > 0 ? height : 1),
      colour_(colour),
      cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {
    clear();
}

void Screen::clear() {
    for (Cell& cell : cells_) {
        cell.glyph = " ";
        cell.style = Style::Normal;
    }
}

Screen::Cell& Screen::at(int x, int y) {
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

const Screen::Cell& Screen::at(int x, int y) const {
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

void Screen::put(int x, int y, const std::string& glyph, Style style) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    Cell& cell = at(x, y);
    cell.glyph = glyph.empty() ? " " : glyph;
    cell.style = style;
}

void Screen::text(int x, int y, const std::string& value, Style style) {
    if (y < 0 || y >= height_) return;
    int column = x;
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '\n') {
            ++i;
            ++y;
            column = x;
            if (y >= height_) return;
            continue;
        }
        if (c == '\r') {
            ++i;
            continue;
        }
        const std::size_t len = glyphLength(value, i);
        if (column >= width_) break;
        put(column, y, value.substr(i, len), style);
        ++column;
        i += len;
    }
}

void Screen::fillRow(int x, int y, int width, const std::string& glyph, Style style) {
    for (int i = 0; i < width; ++i) put(x + i, y, glyph, style);
}

void Screen::hline(int x, int y, int width, Style style) {
    if (y < 0 || y >= height_) return;
    for (int i = 0; i < width; ++i) {
        if (x + i < 0 || x + i >= width_) continue;
        put(x + i, y, "\u2500", style);
    }
}

void Screen::vline(int x, int y, int height, Style style) {
    if (x < 0 || x >= width_) return;
    for (int i = 0; i < height; ++i) {
        if (y + i < 0 || y + i >= height_) continue;
        put(x, y + i, "\u2502", style);
    }
}

void Screen::box(int x, int y, int width, int height, Style border_style) {
    if (width < 2 || height < 2) return;

    put(x, y, "\u256d", border_style);
    put(x + width - 1, y, "\u256e", border_style);
    put(x, y + height - 1, "\u2570", border_style);
    put(x + width - 1, y + height - 1, "\u256f", border_style);

    hline(x + 1, y, width - 2, border_style);
    hline(x + 1, y + height - 1, width - 2, border_style);
    vline(x, y + 1, height - 2, border_style);
    vline(x + width - 1, y + 1, height - 2, border_style);
}

void Screen::boxTitle(int x, int y, int width, const std::string& title, Style style) {
    if (title.empty() || width < 6) return;
    const std::string label = truncate(title, width - 6);
    text(x + 3, y, " " + label + " ", style);
    // Re-draw the border glyphs that the label replaced, keeping the corners.
    put(x, y, "\u256d", Style::Dim);
    put(x + width - 1, y, "\u256e", Style::Dim);
    put(x + 2, y, "\u2500", Style::Dim);
    put(x + 3 + static_cast<int>(splitGlyphs(label).size()) + 1, y, "\u2500", Style::Dim);
}

std::string Screen::render() const {
    std::string out;
    out.reserve(cells_.size() * 4 + 64);

    for (int y = 0; y < height_; ++y) {
        out += "\x1b[" + std::to_string(y + 1) + ";1H";

        Style current = Style::Normal;
        bool wrote_style = false;
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = at(x, y);
            if (colour_ && cell.style != current) {
                out += styleEscape(cell.style);
                current = cell.style;
                wrote_style = true;
            }
            out += cell.glyph;
        }
        if (colour_ && wrote_style) out += "\x1b[0m";
        out += "\x1b[K";
    }
    out += "\x1b[0m";
    return out;
}

std::string Screen::rowText(int y) const {
    if (y < 0 || y >= height_) return {};
    std::string result;
    for (int x = 0; x < width_; ++x) result += at(x, y).glyph;
    return result;
}

std::vector<std::string> Screen::splitGlyphs(const std::string& value) {
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < value.size();) {
        const std::size_t len = glyphLength(value, i);
        glyphs.push_back(value.substr(i, len));
        i += len;
    }
    return glyphs;
}

std::string Screen::truncate(const std::string& value, int max_glyphs) {
    if (max_glyphs <= 0) return {};
    const std::vector<std::string> glyphs = splitGlyphs(value);
    if (static_cast<int>(glyphs.size()) <= max_glyphs) return value;

    std::string result;
    for (int i = 0; i < max_glyphs; ++i) result += glyphs[static_cast<std::size_t>(i)];
    return result;
}

std::string Screen::padRight(const std::string& value, int width) {
    const int length = static_cast<int>(splitGlyphs(value).size());
    if (length >= width) return truncate(value, width);
    return value + std::string(static_cast<std::size_t>(width - length), ' ');
}

std::string Screen::padLeft(const std::string& value, int width) {
    const int length = static_cast<int>(splitGlyphs(value).size());
    if (length >= width) return truncate(value, width);
    return std::string(static_cast<std::size_t>(width - length), ' ') + value;
}

const char* styleEscape(Style style) {
    switch (style) {
        case Style::Normal:       return "\x1b[0m";
        case Style::Dim:          return "\x1b[0;90m";
        case Style::Bold:         return "\x1b[0;97m";
        case Style::Accent:       return "\x1b[0;36m";
        case Style::AccentBold:   return "\x1b[1;36m";
        case Style::Good:         return "\x1b[0;32m";
        case Style::GoodBold:     return "\x1b[1;32m";
        case Style::Warn:         return "\x1b[0;33m";
        case Style::WarnBold:     return "\x1b[1;33m";
        case Style::Bad:          return "\x1b[0;31m";
        case Style::BadBold:      return "\x1b[1;31m";
        case Style::Magenta:      return "\x1b[0;35m";
        case Style::MagentaBold:  return "\x1b[1;35m";
        case Style::Header:       return "\x1b[1;44;97m";
    }
    return "\x1b[0m";
}

} // namespace tui
