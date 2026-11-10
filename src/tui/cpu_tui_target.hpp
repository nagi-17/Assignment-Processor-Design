#ifndef RISC201_TUI_CPU_TARGET_HPP
#define RISC201_TUI_CPU_TARGET_HPP

// ---------------------------------------------------------------------------
// Adapts a pipeline CPU (FourStageCPU / SixStageCPU) to the TuiTarget
// interface.  Header-only and template-based so each pipeline's entry point
// can instantiate it without leaking latch headers into the event loop.
// ---------------------------------------------------------------------------

#include <vector>

#include "app.hpp"
#include "debugger.hpp"

namespace tui {

template <typename Cpu>
class CpuTuiTarget : public TuiTarget {
public:
    explicit CpuTuiTarget(const std::vector<std::string>& program)
        : program_(program) {
        cpu_.loadProgram(program_);
    }

    DebuggerSnapshot getSnapshot() const override { return cpu_.getSnapshot(); }
    bool stepCycle() override { return cpu_.stepCycle(); }
    bool resume() override { return cpu_.resume(); }
    bool readMemoryWord(std::uint32_t address, std::uint32_t& value) const override {
        return cpu_.readMemoryWord(address, value);
    }
    void reload() override { cpu_.loadProgram(program_); }

    Cpu& cpu() { return cpu_; }

private:
    Cpu cpu_{};
    std::vector<std::string> program_;
};

} // namespace tui

#endif // RISC201_TUI_CPU_TARGET_HPP
