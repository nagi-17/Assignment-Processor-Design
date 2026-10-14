#include "debugger_callback_target.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    std::uint32_t pc = 0x100;
    DebuggerCallbacks callbacks;
    callbacks.get_snapshot = [&pc]() {
        DebuggerSnapshot snapshot;
        snapshot.pc = pc;
        return snapshot;
    };
    callbacks.step_cycle = [&pc]() {
        pc += 4;
        return true;
    };
    callbacks.resume = []() { return true; };
    callbacks.read_memory_word = [](std::uint32_t address, std::uint32_t& value) {
        value = address ^ 0x55AAu;
        return true;
    };

    CallbackDebuggerTarget target(callbacks);
    assert(target.getSnapshot().pc == 0x100);
    assert(target.stepCycle());
    assert(target.getSnapshot().pc == 0x104);
    assert(target.resume());
    std::uint32_t value = 0;
    assert(target.readMemoryWord(0x200, value));
    assert(value == (0x200u ^ 0x55AAu));

    CallbackDebuggerTarget incomplete(DebuggerCallbacks{});
    assert(incomplete.getSnapshot().is_stopped);
    assert(incomplete.getSnapshot().stop_reason == DebugStopReason::CpuError);
    assert(!incomplete.stepCycle());
    assert(!incomplete.resume());
    assert(!incomplete.readMemoryWord(0x100, value));

    std::cout << "Callback target tests passed.\n";
    return 0;
}
