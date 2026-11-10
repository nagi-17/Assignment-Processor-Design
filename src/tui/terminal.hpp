#ifndef RISC201_TUI_TERMINAL_HPP
#define RISC201_TUI_TERMINAL_HPP

// ---------------------------------------------------------------------------
// Minimal terminal control layer for the TUI debugger.
//
// Responsibilities:
//   * switch the terminal into raw mode and the alternate screen
//   * report the window size
//   * read one logical key with an optional timeout
//   * restore the original terminal state on destruction
//
// On POSIX the constructor keeps a duplicate of the real terminal descriptor
// and points fd 1 at /dev/null, so stray printf() output from the cache model
// can never corrupt a frame.  Frames are written straight to the saved fd.
// ---------------------------------------------------------------------------

#include <string>

namespace tui {

struct Size {
    int width{80};
    int height{24};
};

enum class KeyType {
    Char,
    Enter,
    Escape,
    Tab,
    Backspace,
    Delete,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    CtrlC,
    Unknown
};

struct Key {
    KeyType type{KeyType::Unknown};
    char ch{0};

    bool is(char c) const { return type == KeyType::Char && ch == c; }
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // Returns false when stdin/stdout are not a terminal.
    bool enter();
    void leave();
    bool active() const { return active_; }

    Size size() const;

    // Writes a complete frame (already containing its own cursor escapes).
    void present(const std::string& frame);

    // Reads one key.  timeout_ms < 0 blocks indefinitely; 0 polls.
    // Returns false when the timeout expires with no input.
    bool readKey(Key& key, int timeout_ms);

    void writeRaw(const std::string& text);

private:
    bool readByte(unsigned char& byte, int timeout_ms);
    bool parseEscape(Key& key);
    void flushOutput();

    bool active_{false};
    int  term_fd_{-1};

#ifdef _WIN32
    unsigned long original_mode_{0};
#else
    struct TerminalState;
    TerminalState* state_{nullptr};
#endif
};

} // namespace tui

#endif // RISC201_TUI_TERMINAL_HPP
