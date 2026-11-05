// test_multicycle_program.hpp
// Shared multi-cycle integration program for the 4-stage and 6-stage CPU tests.
//
// The sequence forces real RAW hazards through multi-cycle units, including two
// back-to-back multi-cycle ops with consumers that depend only on the second:
//   movu r1, 6
//   movu r2, 7
//   mul  r3, r1, r2   -> 42      (4 EX cycles)
//   add  r4, r3, r1   -> 48      (depends on the mul result)
//   mul  r5, r1, r1   -> 36      (multi-cycle immediately before...)
//   div  r6, r2, r1   -> 1       (...another multi-cycle: back-to-back)
//   add  r0, r6, r1   -> 7       (depends on the div result only)
//   add  r7, r5, r2   -> 43      (depends on the second mul result only)

#ifndef TEST_MULTICYCLE_PROGRAM_HPP
#define TEST_MULTICYCLE_PROGRAM_HPP

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace mc {

inline std::vector<std::string> makeProgram() {
    using namespace tf;
    return {
        toBits(encI("movu", 1, 0, 6)),
        toBits(encI("movu", 2, 0, 7)),
        toBits(encR("mul", 3, 1, 2)),
        toBits(encR("add", 4, 3, 1)),
        toBits(encR("mul", 5, 1, 1)),
        toBits(encR("div", 6, 2, 1)),
        toBits(encR("add", 0, 6, 1)),
        toBits(encR("add", 7, 5, 2)),
    };
}

template <typename CPU>
int runAndCheck(const char* name) {
    CPU cpu;
    cpu.loadProgram(makeProgram());

    const std::uint64_t max_cycles = 10000;
    while (!cpu.getSnapshot().is_stopped && cpu.totalCycles() < max_cycles) {
        if (!cpu.stepCycle()) break;
    }

    auto snap = cpu.getSnapshot();
    int fails = 0;
    auto check = [&](const char* what, std::uint32_t got, std::uint32_t want) {
        const bool ok = (got == want);
        if (!ok) ++fails;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << name << " " << what
                  << " = " << got << " (want " << want << ")\n";
    };

    check("r3 (mul result)", snap.registers[3], 42);
    check("r4 (mul consumer)", snap.registers[4], 48);
    check("r5 (2nd mul result)", snap.registers[5], 36);
    check("r6 (div result)", snap.registers[6], 1);
    check("r0 (div consumer)", snap.registers[0], 7);
    check("r7 (2nd mul consumer)", snap.registers[7], 43);

    std::cout << name << ": cycles=" << snap.total_cycles
              << " stalls=" << snap.stall_cycles
              << " retired=" << snap.instructions_retired << "\n";

    // A correct run drains all instructions and shows multi-cycle bubbles.
    if (snap.instructions_retired < 8 || snap.stall_cycles == 0) {
        std::cout << "[ FAIL ] " << name << " did not retire/stall as expected\n";
        ++fails;
    }
    return fails;
}

// A program whose last instruction is multi-cycle. The result must still be
// committed before the CPU decides the program is finished.
//   movu r1, 20
//   movu r2, 4
//   div  r3, r1, r2   -> 5    (last instruction, 16 EX cycles)
inline std::vector<std::string> makeTailProgram() {
    using namespace tf;
    return {
        toBits(encI("movu", 1, 0, 20)),
        toBits(encI("movu", 2, 0, 4)),
        toBits(encR("div", 3, 1, 2)),
    };
}

template <typename CPU>
int runTailAndCheck(const char* name) {
    CPU cpu;
    cpu.loadProgram(makeTailProgram());

    const std::uint64_t max_cycles = 10000;
    while (!cpu.getSnapshot().is_stopped && cpu.totalCycles() < max_cycles) {
        if (!cpu.stepCycle()) break;
    }

    auto snap = cpu.getSnapshot();
    int fails = 0;
    auto check = [&](const char* what, std::uint32_t got, std::uint32_t want) {
        const bool ok = (got == want);
        if (!ok) ++fails;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << name << " " << what
                  << " = " << got << " (want " << want << ")\n";
    };

    check("tail r3 (final div)", snap.registers[3], 5);
    if (snap.instructions_retired < 3) {
        std::cout << "[ FAIL ] " << name << " tail: only retired "
                  << snap.instructions_retired << " instructions\n";
        ++fails;
    }
    return fails;
}

} // namespace mc

#endif // TEST_MULTICYCLE_PROGRAM_HPP
