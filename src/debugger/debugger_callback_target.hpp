#ifndef RISC201_DEBUGGER_CALLBACK_TARGET_HPP
#define RISC201_DEBUGGER_CALLBACK_TARGET_HPP

#include "debugger.hpp"
#include <functional>
#include <utility>

// A small bridge to connect an existing CPU class without making the CPU inherit
// from DebuggerTarget. The team wires these callbacks to its real CPU methods.
struct DebuggerCallbacks {
    std::function<DebuggerSnapshot()> get_snapshot;
    std::function<bool()> step_cycle;
    std::function<bool()> resume;
    std::function<bool(std::uint32_t, std::uint32_t&)> read_memory_word;
};

class CallbackDebuggerTarget : public DebuggerTarget {
public:
    explicit CallbackDebuggerTarget(DebuggerCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    DebuggerSnapshot getSnapshot() const override {
        if (callbacks_.get_snapshot) {
            return callbacks_.get_snapshot();
        }

        DebuggerSnapshot snapshot;
        snapshot.is_stopped = true;
        snapshot.stop_reason = DebugStopReason::CpuError;
        return snapshot;
    }

    bool stepCycle() override {
        return callbacks_.step_cycle && callbacks_.step_cycle();
    }

    bool resume() override {
        return callbacks_.resume && callbacks_.resume();
    }

    bool readMemoryWord(std::uint32_t address, std::uint32_t& value) const override {
        return callbacks_.read_memory_word && callbacks_.read_memory_word(address, value);
    }

private:
    DebuggerCallbacks callbacks_;
};

#endif // RISC201_DEBUGGER_CALLBACK_TARGET_HPP
