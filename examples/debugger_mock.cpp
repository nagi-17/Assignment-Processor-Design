#include "debugger_cli.hpp"

#include <cstdint>
#include <utility>

// This target exists only to demonstrate the CLI before the real CPU coordinator
// is available. It does not execute RISC201 instructions.
class MockCPU : public DebuggerTarget {
public:
    MockCPU() {
        snapshot_.pc = 0x100;
        snapshot_.registers[1] = 7;
        snapshot_.registers[2] = 35;
        refreshPipeline();
    }

    DebuggerSnapshot getSnapshot() const override {
        return snapshot_;
    }

    bool stepCycle() override {
        if (snapshot_.is_stopped) {
            return false;
        }

        snapshot_.pc += 4;
        snapshot_.micro_pc = (snapshot_.micro_pc + 1) % 8;
        snapshot_.has_micro_pc = true;

        if (snapshot_.pc == 0x110) {
            snapshot_.is_stopped = true;
            snapshot_.stop_reason = DebugStopReason::Breakpoint;
        } else if (snapshot_.pc >= 0x120) {
            snapshot_.is_stopped = true;
            snapshot_.stop_reason = DebugStopReason::ProgramHalted;
        }

        refreshPipeline();
        return true;
    }

    bool resume() override {
        if (!snapshot_.is_stopped || snapshot_.stop_reason != DebugStopReason::Breakpoint) {
            return false;
        }
        snapshot_.is_stopped = false;
        snapshot_.stop_reason = DebugStopReason::None;
        return true;
    }

    bool readMemoryWord(std::uint32_t address, std::uint32_t& value) const override {
        if (address < 0x100 || address >= 0x140 || address % 4 != 0) {
            return false;
        }
        value = 0xA5000000u | address;
        return true;
    }

private:
    DebuggerSnapshot snapshot_;

    void refreshPipeline() {
        snapshot_.pipeline.clear();
        snapshot_.pipeline.push_back({"IF/ID", true, {
            {"PC", snapshot_.pc, DebugValueFormat::Hexadecimal},
            {"IR", 0x00000013, DebugValueFormat::Hexadecimal}
        }});
        snapshot_.pipeline.push_back({"ID/EX", (snapshot_.pc >= 0x104), {
            {"OP1", snapshot_.registers[1], DebugValueFormat::Hexadecimal},
            {"OP2", snapshot_.registers[2], DebugValueFormat::Hexadecimal},
            {"IS_ADD", 1, DebugValueFormat::Boolean}
        }});
    }
};

int main() {
    MockCPU mock;
    runDebuggerCLI(mock);
    return 0;
}
