#include "debugger_cli.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

namespace {

void printHelp() {
    cout << "Commands:\n"
         << "  help                         Show available commands\n"
         << "  regs                         Show r0-r15\n"
         << "  reg <register>               Show one register, e.g. reg r5\n"
         << "  pc                           Show the program counter\n"
         << "  flags                        Show CPU flags\n"
         << "  sysregs                      Show available system registers\n"
         << "  pipeline                     Show every pipeline stage\n"
         << "  stage <name>                 Show one pipeline stage\n"
         << "  field <stage> <name>         Show a field in a pipeline stage\n"
         << "  micro                        Show microPC, if available\n"
         << "  status                       Show the current stop status\n"
         << "  step [count]                 Step 1 to 10000 clock cycles\n"
         << "  run                          Run until the CPU stops\n"
         << "  break <address>              Add an aligned breakpoint address\n"
         << "  breaks                       List breakpoint addresses\n"
         << "  delete <address>             Remove a breakpoint address\n"
         << "  mem <address> <count>        Inspect aligned memory words\n"
         << "  quit                         Exit the debugger\n";
}

void printRegisters(const DebuggerSnapshot& snapshot) {
    for (size_t i = 0; i < snapshot.registers.size(); ++i) {
        cout << 'r' << left << setw(2) << i << " = 0x"
             << right << hex << setw(8) << setfill('0') << snapshot.registers[i]
             << setfill(' ') << dec;

        if (i % 4 == 3) {
            cout << '\n';
        } else {
            cout << "    ";
        }
    }
}

void printRegister(const DebuggerSnapshot& snapshot, const string& name) {
    size_t index = 0;
    uint32_t value = 0;
    if (!parseRegisterIndex(name, index) || !getRegisterValue(snapshot, index, value)) {
        cout << "Invalid register. Use r0 through r15.\n";
        return;
    }

    cout << 'r' << index << " = 0x" << hex << setw(8) << setfill('0') << value
         << setfill(' ') << dec << " (" << value << ")\n";
}

void printFlags(const DebuggerSnapshot& snapshot) {
    if (!snapshot.has_flags) {
        cout << "CPU flags are not available in this snapshot.\n";
        return;
    }

    cout << "E = " << (snapshot.flag_e ? 1 : 0)
         << "  GT = " << (snapshot.flag_gt ? 1 : 0) << '\n';
}

void printSystemRegisters(const DebuggerSnapshot& snapshot) {
    bool shown = false;

    if (snapshot.has_epc) {
        cout << "EPC   = " << hexAddress(snapshot.epc) << '\n';
        shown = true;
    }
    if (snapshot.has_cause) {
        cout << "cause = " << snapshot.cause << '\n';
        shown = true;
    }
    if (!shown) {
        cout << "System register values are not available in this snapshot.\n";
    }
}

void printPipeline(const DebuggerSnapshot& snapshot) {
    if (snapshot.pipeline.empty()) {
        cout << "No pipeline state is available.\n";
        return;
    }

    for (const PipelineStageSnapshot& stage : snapshot.pipeline) {
        cout << left << setw(10) << stage.stage_name
             << (stage.is_valid ? "valid instruction" : "empty/bubble") << '\n';
    }
}

void printStage(const DebuggerSnapshot& snapshot, const string& stageName) {
    const PipelineStageSnapshot* stage = findPipelineStage(snapshot, stageName);
    if (stage == nullptr) {
        cout << "Stage not found: " << stageName << '\n';
        return;
    }

    cout << stage->stage_name << " (" << (stage->is_valid ? "valid" : "empty/bubble") << ")\n";
    if (stage->fields.empty()) {
        cout << "  No fields are available for this stage.\n";
        return;
    }

    for (const DebugField& field : stage->fields) {
        cout << "  " << field.name << " = " << formatValue(field.value, field.format) << '\n';
    }
}

void printField(const DebuggerSnapshot& snapshot, const string& stageName, const string& fieldName) {
    const DebugField* field = findPipelineField(snapshot, stageName, fieldName);
    if (field == nullptr) {
        cout << "Field not found: " << fieldName << " in stage " << stageName << '\n';
        return;
    }

    cout << stageName << '.' << field->name << " = "
         << formatValue(field->value, field->format) << '\n';
}

void printStatus(const DebuggerSnapshot& snapshot) {
    cout << "CPU status: " << (snapshot.is_stopped ? "stopped" : "running") << '\n';
    if (snapshot.is_stopped) {
        cout << "Stop reason: " << stopReasonName(snapshot.stop_reason) << '\n';
    }
}

void printCycleUpdate(const DebuggerSnapshot& snapshot, uint64_t cycleNumber) {
    cout << "Cycle " << cycleNumber << ": PC = " << hexAddress(snapshot.pc);
    if (snapshot.has_micro_pc) {
        cout << "  microPC = " << hexAddress(snapshot.micro_pc);
    } else {
        cout << "  microPC unavailable";
    }
    cout << '\n';
}

} // namespace

void runDebuggerCLI(DebuggerTarget& cpu) {
    vector<uint64_t> breakpoints;
    bool pausedAtAddressBreakpoint = false;
    uint64_t pausedAddress = 0;
    bool skipAddressBreakpointOnce = false;

    cout << "RISC201 Debugger\nType 'help' to see available commands.\n";

    string line;
    while (true) {
        cout << "debug> ";
        if (!getline(cin, line)) {
            break;
        }

        istringstream input(line);
        string command;
        input >> command;
        if (command.empty()) {
            continue;
        }

        DebuggerSnapshot snapshot = cpu.getSnapshot();
        if (pausedAtAddressBreakpoint && snapshot.pc == pausedAddress) {
            snapshot.is_stopped = true;
            snapshot.stop_reason = DebugStopReason::AddressBreakpoint;
        }

        if (command == "help" || command == "h") {
            printHelp();
        } else if (command == "regs" || command == "registers") {
            printRegisters(snapshot);
        } else if (command == "reg") {
            string name;
            if (input >> name) printRegister(snapshot, name);
            else cout << "Usage: reg <register>, e.g. reg r5\n";
        } else if (command == "pc") {
            cout << "PC = " << hexAddress(snapshot.pc) << '\n';
        } else if (command == "flags") {
            printFlags(snapshot);
        } else if (command == "sysregs") {
            printSystemRegisters(snapshot);
        } else if (command == "pipeline") {
            printPipeline(snapshot);
        } else if (command == "stage") {
            string stageName;
            if (input >> stageName) printStage(snapshot, stageName);
            else cout << "Usage: stage <name>\n";
        } else if (command == "field") {
            string stageName, fieldName;
            if (input >> stageName >> fieldName) printField(snapshot, stageName, fieldName);
            else cout << "Usage: field <stage> <name>\n";
        } else if (command == "micro" || command == "micropc") {
            if (snapshot.has_micro_pc) cout << "microPC = " << hexAddress(snapshot.micro_pc) << '\n';
            else cout << "microPC is not available.\n";
        } else if (command == "status") {
            printStatus(snapshot);
        } else if (command == "step") {
            string countText;
            uint64_t count = 1;
            if (input >> countText) {
                if (!parseNumber(countText, count) || count == 0 || count > 10000) {
                    cout << "Usage: step [count] (count must be from 1 to 10000)\n";
                    continue;
                }
            }

            if (pausedAtAddressBreakpoint) {
                pausedAtAddressBreakpoint = false;
            } else if (snapshot.is_stopped) {
                if (snapshot.stop_reason != DebugStopReason::Breakpoint || !cpu.resume()) {
                    printStatus(snapshot);
                    cout << "CPU could not resume for stepping.\n";
                    continue;
                }
            }

            uint64_t completed = 0;
            for (uint64_t i = 0; i < count; ++i) {
                if (!cpu.stepCycle()) {
                    cout << "CPU could not advance another cycle.\n";
                    break;
                }
                ++completed;
                snapshot = cpu.getSnapshot();
                printCycleUpdate(snapshot, completed);
                if (snapshot.is_stopped) {
                    printStatus(snapshot);
                    break;
                }
            }
            cout << "Stepped " << completed << " cycle(s).\n";
        } else if (command == "run" || command == "continue" || command == "c") {
            if (pausedAtAddressBreakpoint) {
                skipAddressBreakpointOnce = true;
                pausedAtAddressBreakpoint = false;
            } else if (snapshot.is_stopped) {
                if (snapshot.stop_reason != DebugStopReason::Breakpoint || !cpu.resume()) {
                    printStatus(snapshot);
                    cout << "CPU cannot continue from this stop reason.\n";
                    continue;
                }
            }

            uint64_t cycles = 0;
            const uint64_t maxCycles = 1000000;
            bool stopped = false;

            while (cycles < maxCycles) {
                snapshot = cpu.getSnapshot();
                if (snapshot.is_stopped) {
                    printStatus(snapshot);
                    printPipeline(snapshot);
                    stopped = true;
                    break;
                }

                if (skipAddressBreakpointOnce && snapshot.pc == pausedAddress) {
                    skipAddressBreakpointOnce = false;
                } else if (find(breakpoints.begin(), breakpoints.end(), snapshot.pc) != breakpoints.end()) {
                    pausedAtAddressBreakpoint = true;
                    pausedAddress = snapshot.pc;
                    cout << "Debugger breakpoint hit at " << hexAddress(snapshot.pc) << ".\n";
                    stopped = true;
                    break;
                }

                if (!cpu.stepCycle()) {
                    snapshot = cpu.getSnapshot();
                    cout << "CPU could not advance further.\n";
                    printStatus(snapshot);
                    stopped = true;
                    break;
                }

                ++cycles;
                snapshot = cpu.getSnapshot();
                printCycleUpdate(snapshot, cycles);
                if (snapshot.is_stopped) {
                    printStatus(snapshot);
                    printPipeline(snapshot);
                    stopped = true;
                    break;
                }
            }

            if (!stopped) {
                cout << "Run stopped at the safety limit of " << maxCycles << " cycles.\n";
            } else {
                cout << "Run ended after " << cycles << " cycle(s).\n";
            }
        } else if (command == "break") {
            string addressText;
            uint64_t address = 0;
            if (!(input >> addressText) || !parseNumber(addressText, address) ||
                address > numeric_limits<uint32_t>::max()) {
                cout << "Usage: break <32-bit address>, e.g. break 0x100\n";
                continue;
            }
            if (address % 4 != 0) {
                cout << "Breakpoint address must be 4-byte aligned.\n";
                continue;
            }
            if (find(breakpoints.begin(), breakpoints.end(), address) != breakpoints.end()) {
                cout << "Breakpoint already exists at " << hexAddress(address) << '\n';
            } else {
                breakpoints.push_back(address);
                cout << "Breakpoint added at " << hexAddress(address) << '\n';
            }
        } else if (command == "breaks") {
            if (breakpoints.empty()) {
                cout << "No breakpoints set.\n";
            } else {
                for (uint64_t address : breakpoints) cout << hexAddress(address) << '\n';
            }
        } else if (command == "delete") {
            string addressText;
            uint64_t address = 0;
            if (!(input >> addressText) || !parseNumber(addressText, address) ||
                address > numeric_limits<uint32_t>::max() || address % 4 != 0) {
                cout << "Usage: delete <aligned 32-bit address>, e.g. delete 0x100\n";
                continue;
            }
            auto it = find(breakpoints.begin(), breakpoints.end(), address);
            if (it == breakpoints.end()) {
                cout << "No breakpoint exists at " << hexAddress(address) << '\n';
            } else {
                breakpoints.erase(it);
                cout << "Breakpoint removed from " << hexAddress(address) << '\n';
            }
        } else if (command == "mem") {
            string addressText, countText;
            uint64_t address = 0, count = 0;
            if (!(input >> addressText >> countText) ||
                !parseNumber(addressText, address) ||
                !parseNumber(countText, count) || count == 0 || count > 4096 ||
                address > numeric_limits<uint32_t>::max() ||
                address + (count - 1) * 4 > numeric_limits<uint32_t>::max()) {
                cout << "Usage: mem <32-bit address> <count> (count from 1 to 4096)\n";
                continue;
            }
            if (address % 4 != 0) {
                cout << "Memory address must be 4-byte aligned.\n";
                continue;
            }

            for (uint64_t i = 0; i < count; ++i) {
                uint32_t value = 0;
                uint32_t currentAddress = static_cast<uint32_t>(address + i * 4);
                if (!cpu.readMemoryWord(currentAddress, value)) {
                    cout << "Could not read memory at " << hexAddress(currentAddress) << '\n';
                    break;
                }
                cout << hexAddress(currentAddress) << "  0x"
                     << hex << right << setw(8) << setfill('0') << value
                     << setfill(' ') << dec << '\n';
            }
        } else if (command == "quit" || command == "q" || command == "exit") {
            break;
        } else {
            cout << "Unknown command: " << command << ". Type 'help' for commands.\n";
        }
    }
}
