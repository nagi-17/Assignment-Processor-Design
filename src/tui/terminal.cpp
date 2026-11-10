#include "terminal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace tui {

#ifndef _WIN32

struct Terminal::TerminalState {
    termios original{};
    bool have_original{false};
    int  saved_stdout{-1};
};

namespace {

constexpr int kEscapeTimeoutMs = 40;

} // namespace

Terminal::Terminal() = default;

Terminal::~Terminal() { leave(); }

bool Terminal::enter() {
    if (active_) return true;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;

    state_ = new TerminalState();
    if (tcgetattr(STDIN_FILENO, &state_->original) != 0) {
        delete state_;
        state_ = nullptr;
        return false;
    }
    state_->have_original = true;

    termios raw = state_->original;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        delete state_;
        state_ = nullptr;
        return false;
    }

    // Keep the real terminal on a private descriptor and silence fd 1.  The
    // cache model prints hit/miss traces with printf(), and those must not be
    // able to land in the middle of a frame.
    state_->saved_stdout = dup(STDOUT_FILENO);
    if (state_->saved_stdout >= 0) {
        term_fd_ = state_->saved_stdout;
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
    } else {
        term_fd_ = STDOUT_FILENO;
    }

    active_ = true;
    writeRaw("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
    return true;
}

void Terminal::leave() {
    if (!active_) return;
    writeRaw("\x1b[2J\x1b[H\x1b[?25h\x1b[?1049l");
    flushOutput();

    if (state_ && state_->saved_stdout >= 0) {
        // Bring the real terminal back onto fd 1 before restoring attributes.
        fflush(stdout);
        dup2(state_->saved_stdout, STDOUT_FILENO);
        close(state_->saved_stdout);
        state_->saved_stdout = -1;
        term_fd_ = STDOUT_FILENO;
    }

    if (state_ && state_->have_original) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &state_->original);
    }
    delete state_;
    state_ = nullptr;
    active_ = false;
}

Size Terminal::size() const {
    winsize ws{};
    const int fd = term_fd_ >= 0 ? term_fd_ : STDOUT_FILENO;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return Size{static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
    }
    return Size{};
}

void Terminal::flushOutput() {
    if (term_fd_ < 0) return;
}

void Terminal::present(const std::string& frame) {
    if (!active_) return;
    std::string out;
    out.reserve(frame.size() + 16);
    out += "\x1b[H";
    out += frame;
    writeRaw(out);
}

void Terminal::writeRaw(const std::string& text) {
    const int fd = term_fd_ >= 0 ? term_fd_ : STDOUT_FILENO;
    std::size_t written = 0;
    while (written < text.size()) {
        ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += static_cast<std::size_t>(n);
    }
}

bool Terminal::readByte(unsigned char& byte, int timeout_ms) {
    if (timeout_ms >= 0) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeval tv{};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        const int ready = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
        if (ready <= 0) return false;
    }

    const ssize_t got = ::read(STDIN_FILENO, &byte, 1);
    return got == 1;
}

bool Terminal::parseEscape(Key& key) {
    unsigned char next = 0;
    if (!readByte(next, kEscapeTimeoutMs)) {
        key.type = KeyType::Escape;
        return true;
    }

    if (next != '[' && next != 'O') {
        key.type = KeyType::Escape;
        return true;
    }

    unsigned char code = 0;
    if (!readByte(code, kEscapeTimeoutMs)) {
        key.type = KeyType::Escape;
        return true;
    }

    std::string params;
    while (code >= '0' && code <= '9') {
        params.push_back(static_cast<char>(code));
        if (!readByte(code, kEscapeTimeoutMs)) {
            key.type = KeyType::Escape;
            return true;
        }
    }

    if (code == '~') {
        if (params == "3")      key.type = KeyType::Delete;
        else if (params == "5") key.type = KeyType::PageUp;
        else if (params == "6") key.type = KeyType::PageDown;
        else if (params == "1" || params == "7") key.type = KeyType::Home;
        else if (params == "4" || params == "8") key.type = KeyType::End;
        else                     key.type = KeyType::Unknown;
        return true;
    }

    switch (code) {
        case 'A': key.type = KeyType::Up;      break;
        case 'B': key.type = KeyType::Down;    break;
        case 'C': key.type = KeyType::Right;   break;
        case 'D': key.type = KeyType::Left;    break;
        case 'H': key.type = KeyType::Home;    break;
        case 'F': key.type = KeyType::End;     break;
        default:  key.type = KeyType::Unknown; break;
    }
    return true;
}

bool Terminal::readKey(Key& key, int timeout_ms) {
    key = Key{};

    unsigned char byte = 0;
    if (!readByte(byte, timeout_ms)) return false;

    switch (byte) {
        case 0x03: key.type = KeyType::CtrlC;     return true;
        case 0x1B: return parseEscape(key);
        case '\r':
        case '\n': key.type = KeyType::Enter;     return true;
        case '\t': key.type = KeyType::Tab;       return true;
        case 0x7F:
        case 0x08: key.type = KeyType::Backspace; return true;
        default:
            key.type = KeyType::Char;
            key.ch   = static_cast<char>(byte);
            return true;
    }
}

#else // _WIN32

struct Terminal::TerminalState {};

Terminal::Terminal() : term_fd_(-1) {}
Terminal::~Terminal() { leave(); }

bool Terminal::enter() {
    if (active_) return true;

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode)) return false;
    original_mode_ = mode;
    SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    active_ = true;
    writeRaw("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
    return true;
}

void Terminal::leave() {
    if (!active_) return;
    writeRaw("\x1b[2J\x1b[H\x1b[?25h\x1b[?1049l");
    std::fflush(stdout);

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE) SetConsoleMode(out, original_mode_);
    active_ = false;
}

Size Terminal::size() const {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(out, &info)) {
        return Size{info.srWindow.Right - info.srWindow.Left + 1,
                    info.srWindow.Bottom - info.srWindow.Top + 1};
    }
    return Size{};
}

void Terminal::flushOutput() { std::fflush(stdout); }

void Terminal::present(const std::string& frame) {
    if (!active_) return;
    std::string out = "\x1b[H" + frame;
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

void Terminal::writeRaw(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

bool Terminal::readByte(unsigned char& byte, int timeout_ms) {
    if (timeout_ms >= 0) {
        if (!_kbhit()) {
            Sleep(static_cast<DWORD>(timeout_ms));
            if (!_kbhit()) return false;
        }
    }
    const int c = _getch();
    if (c == -1) return false;
    byte = static_cast<unsigned char>(c);
    return true;
}

bool Terminal::parseEscape(Key& key) {
    unsigned char next = 0;
    if (!readByte(next, 40)) {
        key.type = KeyType::Escape;
        return true;
    }
    if (next == 0 || next == 0xE0) {
        unsigned char code = 0;
        if (!readByte(code, 40)) {
            key.type = KeyType::Escape;
            return true;
        }
        switch (code) {
            case 72: key.type = KeyType::Up;       break;
            case 80: key.type = KeyType::Down;     break;
            case 75: key.type = KeyType::Left;     break;
            case 77: key.type = KeyType::Right;    break;
            case 71: key.type = KeyType::Home;     break;
            case 79: key.type = KeyType::End;      break;
            case 73: key.type = KeyType::PageUp;   break;
            case 81: key.type = KeyType::PageDown; break;
            case 83: key.type = KeyType::Delete;   break;
            default: key.type = KeyType::Unknown;  break;
        }
        return true;
    }
    key.type = KeyType::Escape;
    return true;
}

bool Terminal::readKey(Key& key, int timeout_ms) {
    key = Key{};
    unsigned char byte = 0;
    if (!readByte(byte, timeout_ms)) return false;

    switch (byte) {
        case 0x03: key.type = KeyType::CtrlC;     return true;
        case 0x1B: return parseEscape(key);
        case '\r':
        case '\n': key.type = KeyType::Enter;     return true;
        case '\t': key.type = KeyType::Tab;       return true;
        case 0x08: key.type = KeyType::Backspace; return true;
        default:
            key.type = KeyType::Char;
            key.ch   = static_cast<char>(byte);
            return true;
    }
}

#endif

} // namespace tui
