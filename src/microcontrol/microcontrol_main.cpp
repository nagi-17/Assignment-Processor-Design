// ---------------------------------------------------------------------------
// CLI for the standalone microcontrol model.
//
// Usage:
//   ./microcontrol_simulator <program.bin|program.hex> [--4|--6] [--trace]
//
// Interactive commands: load, run4, run6, step4, step6, regs, pc, pipeline,
// micro, trace, mem <addr>, cycles, reset, help, quit.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "microprogrammed_cpu.hpp"

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool isBinaryWord(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s)
        if (c != '0' && c != '1') return false;
    return true;
}

bool isHexWord(const std::string& s) {
    if (s.empty() || s.size() > 8) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string toBinaryWord(std::uint32_t v) {
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i) {
        s[static_cast<std::size_t>(31 - i)] = ((v >> i) & 1u) ? '1' : '0';
    }
    return s;
}

bool loadProgramFile(const std::string& path, std::vector<std::string>& out,
                     std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open '" + path + "'";
        return false;
    }

    out.clear();
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (isBinaryWord(line)) {
            out.push_back(line);
            continue;
        }
        if (isHexWord(line)) {
            std::uint32_t v = static_cast<std::uint32_t>(std::stoul(line, nullptr, 16));
            out.push_back(toBinaryWord(v));
            continue;
        }
        std::ostringstream oss;
        oss << path << ":" << line_no << ": expected a 32-bit binary word or a hex word";
        error = oss.str();
        return false;
    }

    if (out.empty()) {
        error = "no instructions found in '" + path + "'";
        return false;
    }
    return true;
}

void printBanner() {
    std::cout
        << "===========================================================\n"
        << "  RISC201 Microcontrol Model\n"
        << "  a microcontrol execution unit behind every pipeline latch\n"
        << "  4-stage: IF ID EX MEM/WB      6-stage: IF ID EX MA1 MA2 WB\n"
        << "===========================================================\n"
        << "Type 'help' for commands.\n\n";
}

void printHelp() {
    std::cout
        << "  load <file>     load a .bin (32-bit binary words) or .hex program\n"
        << "  run4 / run6     run the whole program on the 4/6-stage model\n"
        << "  step4 / step6   create/advance the 4/6-stage model by one clock\n"
        << "  regs            show the register file\n"
        << "  pc              show the fetch PC\n"
        << "  pipeline        show the pipeline latches\n"
        << "  micro           show every stage's microPC, MIR and shared bus\n"
        << "  trace           show the last clock's micro-op sequence\n"
        << "  mem <addr>      read one data-memory word\n"
        << "  cycles          show cycle / stall / retired statistics\n"
        << "  reset           reset the current model\n"
        << "  help            this text\n"
        << "  quit            exit\n";
}

void printStats(const mc::MicroprogrammedCPU& cpu) {
    std::cout << "  model:     " << (cpu.fourStage() ? "4-stage" : "6-stage") << "\n"
              << "  cycles:    " << cpu.totalCycles() << "\n"
              << "  stalls:    " << cpu.stallCycles() << "\n"
              << "  retired:   " << cpu.instructionsRetired() << "\n"
              << "  halted:    " << (cpu.halted() ? "yes" : "no") << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> program;
    std::string program_path;
    bool want_four = false;
    bool want_six = false;
    bool trace = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--4" || arg == "-4" || arg == "--4stage") want_four = true;
        else if (arg == "--6" || arg == "-6" || arg == "--6stage") want_six = true;
        else if (arg == "--trace" || arg == "-t") trace = true;
        else if (arg == "--help" || arg == "-h") {
            printBanner();
            printHelp();
            return 0;
        } else if (program_path.empty()) {
            program_path = arg;
        }
    }

    std::unique_ptr<mc::MicroprogrammedCPU> cpu;
    bool current_four = false;

    auto ensure_cpu = [&](bool four) -> bool {
        if (!cpu || current_four != four) {
            cpu = std::make_unique<mc::MicroprogrammedCPU>(four, trace);
            if (!program.empty())
                cpu->loadProgram(program);
            current_four = four;
        }
        return true;
    };

    if (!program_path.empty()) {
        std::string error;
        if (!loadProgramFile(program_path, program, error)) {
            std::cerr << "Error: " << error << "\n";
            return 1;
        }
        std::cout << "Loaded " << program.size() << " instructions from "
                  << program_path << "\n";

        auto run_model = [&](bool four) {
            mc::MicroprogrammedCPU model(four, trace);
            model.loadProgram(program);
            model.run();
            std::cout << "\n===== " << (four ? "4-stage" : "6-stage")
                      << " result =====\n";
            model.dumpRegisters();
            printStats(model);
        };

        if (want_four && !want_six) run_model(true);
        else if (want_six && !want_four) run_model(false);
        else {
            run_model(true);
            run_model(false);
        }
        return 0;
    }

    printBanner();
    std::string line;
    while (true) {
        std::cout << "microcontrol> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "help" || cmd == "h") {
            printHelp();
        } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            break;
        } else if (cmd == "load") {
            std::string path;
            if (!(iss >> path)) {
                std::cout << "Usage: load <file.bin|file.hex>\n";
                continue;
            }
            std::string error;
            std::vector<std::string> loaded;
            if (!loadProgramFile(path, loaded, error)) {
                std::cout << "Error: " << error << "\n";
            } else {
                program = std::move(loaded);
                program_path = path;
                cpu.reset();
                std::cout << "Loaded " << program.size() << " instructions from "
                          << path << "\n";
            }
        } else if (cmd == "run4" || cmd == "run6") {
            const bool four = (cmd == "run4");
            if (program.empty()) {
                std::cout << "No program loaded.\n";
                continue;
            }
            ensure_cpu(four);
            cpu->run();
            std::cout << "\n===== " << (four ? "4-stage" : "6-stage")
                      << " result =====\n";
            cpu->dumpRegisters();
            printStats(*cpu);
        } else if (cmd == "step4" || cmd == "step6") {
            const bool four = (cmd == "step4");
            if (program.empty()) {
                std::cout << "No program loaded.\n";
                continue;
            }
            ensure_cpu(four);
            cpu->step();
            std::cout << "cycle " << cpu->totalCycles()
                      << "  PC=" << cpu->pc()
                      << "  halted=" << (cpu->halted() ? "yes" : "no") << "\n";
            cpu->dumpPipeline();
            cpu->dumpMicro();
        } else if (cmd == "regs") {
            if (!cpu) { std::cout << "No model active. Use step4/step6/run4/run6.\n"; continue; }
            cpu->dumpRegisters();
        } else if (cmd == "pc") {
            if (!cpu) { std::cout << "No model active.\n"; continue; }
            std::cout << "PC = " << cpu->pc() << "\n";
        } else if (cmd == "pipeline" || cmd == "pipe") {
            if (!cpu) { std::cout << "No model active.\n"; continue; }
            cpu->dumpPipeline();
        } else if (cmd == "micro" || cmd == "micropc") {
            if (!cpu) { std::cout << "No model active.\n"; continue; }
            cpu->dumpMicro();
        } else if (cmd == "trace" || cmd == "microtrace") {
            if (!cpu) { std::cout << "No model active.\n"; continue; }
            cpu->dumpTrace();
        } else if (cmd == "mem") {
            std::string addr_text;
            if (!(iss >> addr_text) || !cpu) {
                std::cout << "Usage: mem <address> (after starting a model)\n";
                continue;
            }
            std::uint64_t addr = 0;
            try {
                addr = std::stoull(addr_text, nullptr, 0);
            } catch (...) {
                std::cout << "Bad address.\n";
                continue;
            }
            std::uint32_t value = 0;
            cpu->readMemory(static_cast<std::uint32_t>(addr), value);
            std::cout << "mem[0x" << std::hex << addr << "] = 0x"
                      << std::setw(8) << std::setfill('0') << value
                      << std::dec << std::setfill(' ') << "\n";
        } else if (cmd == "cycles" || cmd == "stats") {
            if (!cpu) { std::cout << "No model active.\n"; continue; }
            printStats(*cpu);
        } else if (cmd == "reset") {
            if (cpu) {
                cpu->reset();
                std::cout << "Model reset.\n";
            } else {
                std::cout << "No model active.\n";
            }
        } else {
            std::cout << "Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    }

    return 0;
}
