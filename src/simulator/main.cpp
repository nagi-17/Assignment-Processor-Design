// main.cpp
// Top-level CLI and Simulator Driver for the RISC201 Processor.
//
// Allows assembling RISC201 assembly files, executing on either the 4-stage
// or 6-stage pipelined processor, launching the interactive step-by-step
// debugger, and comparing the clock-cycle performance between both pipelines.

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "runner_interface.hpp"
#include "engine_session.hpp"
#include "entry.hpp"

// Assembler components
#include "../assembler/preprocessor.cpp"
#include "../assembler/symbol_table.cpp"
#include "../assembler/bin_generator.cpp"

namespace {

bool parseCycleCount(const std::string& text, uint64_t& value) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    try {
        value = std::stoull(text);
        return true;
    } catch (...) {
        return false;
    }
}

void printBanner() {
    std::cout << "\n"
              << "========================================================================\n"
              << "               RISC201 Processor Simulation Environment                \n"
              << "       Computer Organization & Architecture (CSC-201) Assignment       \n"
              << "       Supports 4-Stage & 6-Stage Pipelines with Microprogrammed CU    \n"
              << "========================================================================\n"
              << "Type 'help' for available commands, or 'load <file.s>' to load code.\n\n";
}

void printHelp() {
    std::cout << "Available Commands:\n"
              << "  load <file.s|file.bin>    Load and auto-assemble a source program\n"
              << "  assemble <in.s> [out.bin] Assemble source code into binary file\n"
              << "  run4                      Execute loaded program on 4-Stage Pipeline\n"
              << "  run6                      Execute loaded program on 6-Stage Pipeline\n"
              << "  debug4                    Interactive step-by-step debugger (4-Stage)\n"
              << "  debug6                    Interactive step-by-step debugger (6-Stage)\n"
              << "  tui4 / tui6               Full-screen TUI debugger (4-Stage / 6-Stage)\n"
              << "  compare                   Run on BOTH pipelines and compare cycle stats\n"
              << "  pipeline [4|6] [n|run|reset]\n"
              << "                            Show control-ROM pipeline latches + control ROM\n"
              << "  micropipeline [4|6] [n|run|reset|all]\n"
              << "                            Show microprogrammed-CU pipeline + microprograms\n"
              << "                              n   advance that many clocks first\n"
              << "                              run advance to completion, reset restart\n"
              << "                              all dump the whole microprogram store\n"
              << "  program                   Display currently loaded binary instructions\n"
              << "  stats                     Display stats from the most recent run\n"
              << "  help                      Display this help menu\n"
              << "  quit / exit               Exit simulator\n\n"
              << "Examples:\n"
              << "  load tests/test_basic.s\n"
              << "  run4\n"
              << "  run6\n"
              << "  compare\n"
              << "  debug4\n"
              << "  tui6\n"
              << "  pipeline 4 5\n"
              << "  micropipeline 6 all\n\n";
}

bool assembleSource(const std::string& inputPath,
                    std::vector<std::string>& outBinary,
                    std::string& errorMsg) {
    std::ifstream infile(inputPath);
    if (!infile.is_open()) {
        errorMsg = "Could not open source file: " + inputPath;
        return false;
    }

    std::vector<std::string> raw_lines;
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        raw_lines.push_back(line);
    }

    try {
        Preprocessor preprocessor;
        std::vector<std::string> clean_lines = preprocessor.process(raw_lines);

        PassOne pass_one;
        pass_one.run(clean_lines);
        auto pure_instructions = pass_one.get_pure_instructions();
        auto symbol_table = pass_one.get_symbol_table();

        BinaryGenerator pass_two;
        outBinary = pass_two.generate(pure_instructions, symbol_table);
        return true;
    } catch (const std::exception& e) {
        errorMsg = std::string("Assembly error: ") + e.what();
        return false;
    }
}

bool loadBinaryFile(const std::string& inputPath,
                    std::vector<std::string>& outBinary,
                    std::string& errorMsg) {
    std::ifstream infile(inputPath);
    if (!infile.is_open()) {
        errorMsg = "Could not open binary file: " + inputPath;
        return false;
    }

    outBinary.clear();
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t");
        std::string trimmed = line.substr(first, last - first + 1);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
        outBinary.push_back(trimmed);
    }

    if (outBinary.empty()) {
        errorMsg = "Binary file is empty: " + inputPath;
        return false;
    }
    return true;
}

bool loadProgramAuto(const std::string& path,
                     std::vector<std::string>& outBinary,
                     std::string& errorMsg) {
    if (path.size() >= 2 && path.substr(path.size() - 2) == ".s") {
        return assembleSource(path, outBinary, errorMsg);
    }

    std::vector<std::string> lines;
    if (loadBinaryFile(path, lines, errorMsg)) {
        bool looksLikeBinary = true;
        for (const auto& l : lines) {
            if (l.size() != 32) { looksLikeBinary = false; break; }
            for (char c : l) {
                if (c != '0' && c != '1') { looksLikeBinary = false; break; }
            }
            if (!looksLikeBinary) break;
        }
        if (looksLikeBinary) {
            outBinary = lines;
            return true;
        }
    }

    return assembleSource(path, outBinary, errorMsg);
}

std::string formatTime(double ns) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (ns >= 1.0e6)      oss << (ns / 1.0e6) << " ms";
    else if (ns >= 1.0e3) oss << (ns / 1.0e3) << " us";
    else                  oss << ns << " ns";
    return oss.str();
}

void printComparison(const PipelineStats& s4, const PipelineStats& s6) {
    std::cout << "\n========================================================================\n";
    std::cout << "                 RISC201 PIPELINE EXECUTION COMPARISON                  \n";
    std::cout << "========================================================================\n";
    std::cout << std::left << std::setw(30) << "Metric"
              << std::setw(20) << "4-Stage Pipeline"
              << std::setw(20) << "6-Stage Pipeline" << "\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(30) << "Instructions Retired"
              << std::setw(20) << s4.instructions_retired
              << std::setw(20) << s6.instructions_retired << "\n";
    std::cout << std::left << std::setw(30) << "Total Clock Cycles"
              << std::setw(20) << s4.total_cycles
              << std::setw(20) << s6.total_cycles << "\n";

    std::ostringstream cp4_s, cp6_s;
    cp4_s << std::fixed << std::setprecision(2) << s4.clock_period_ns;
    cp6_s << std::fixed << std::setprecision(2) << s6.clock_period_ns;
    std::cout << std::left << std::setw(30) << "Clock Period (ns)"
              << std::setw(20) << cp4_s.str()
              << std::setw(20) << cp6_s.str() << "\n";

    std::cout << std::left << std::setw(30) << "Total Execution Time"
              << std::setw(20) << formatTime(s4.total_time_ns)
              << std::setw(20) << formatTime(s6.total_time_ns) << "\n";
    std::cout << std::left << std::setw(30) << "Stall Cycles"
              << std::setw(20) << s4.stall_cycles
              << std::setw(20) << s6.stall_cycles << "\n";

    double pct4 = (s4.total_cycles > 0) ? (static_cast<double>(s4.stall_cycles) / s4.total_cycles) * 100.0 : 0.0;
    double pct6 = (s6.total_cycles > 0) ? (static_cast<double>(s6.stall_cycles) / s6.total_cycles) * 100.0 : 0.0;

    std::ostringstream ss4, ss6;
    ss4 << std::fixed << std::setprecision(1) << pct4 << "%";
    ss6 << std::fixed << std::setprecision(1) << pct6 << "%";

    std::cout << std::left << std::setw(30) << "Stall Overhead"
              << std::setw(20) << ss4.str()
              << std::setw(20) << ss6.str() << "\n";

    std::ostringstream cpi4_s, cpi6_s;
    cpi4_s << std::fixed << std::setprecision(2) << s4.cpi;
    cpi6_s << std::fixed << std::setprecision(2) << s6.cpi;

    std::cout << std::left << std::setw(30) << "CPI (Cycles/Instruction)"
              << std::setw(20) << cpi4_s.str()
              << std::setw(20) << cpi6_s.str() << "\n";
    std::cout << "------------------------------------------------------------------------\n";

    if (s4.total_cycles > 0 && s6.total_cycles > 0) {
        if (s4.total_cycles < s6.total_cycles) {
            double diff = ((static_cast<double>(s6.total_cycles) - s4.total_cycles) / s4.total_cycles) * 100.0;
            std::cout << "  Analysis: 4-stage pipeline is " << std::fixed << std::setprecision(1) << diff
                      << "% faster (fewer stall cycles across shorter hazard window).\n";
        } else if (s6.total_cycles < s4.total_cycles) {
            double diff = ((static_cast<double>(s4.total_cycles) - s6.total_cycles) / s6.total_cycles) * 100.0;
            std::cout << "  Analysis: 6-stage pipeline completed with fewer cycles (" << std::fixed << std::setprecision(1) << diff << "% difference).\n";
        } else {
            std::cout << "  Analysis: Both pipelines completed in the exact same number of clock cycles.\n";
        }
    }

    if (s4.total_time_ns > 0.0 && s6.total_time_ns > 0.0) {
        if (s4.total_time_ns < s6.total_time_ns) {
            double diff = ((s6.total_time_ns - s4.total_time_ns) / s4.total_time_ns) * 100.0;
            std::cout << "  Wall-clock: 4-stage finishes sooner (" << formatTime(s4.total_time_ns)
                      << " vs " << formatTime(s6.total_time_ns) << ", " << std::fixed
                      << std::setprecision(1) << diff << "% faster).\n";
        } else if (s6.total_time_ns < s4.total_time_ns) {
            double diff = ((s4.total_time_ns - s6.total_time_ns) / s6.total_time_ns) * 100.0;
            std::cout << "  Wall-clock: 6-stage finishes sooner (" << formatTime(s6.total_time_ns)
                      << " vs " << formatTime(s4.total_time_ns) << ", " << std::fixed
                      << std::setprecision(1) << diff << "% faster).\n";
        } else {
            std::cout << "  Wall-clock: Both pipelines take the same time ("
                      << formatTime(s4.total_time_ns) << ").\n";
        }
    }
    std::cout << "========================================================================\n\n";
}

void printStats(const PipelineStats& s) {
    std::cout << "\n=======================================================\n";
    std::cout << "  Execution Results: " << s.pipeline_name << "\n";
    std::cout << "=======================================================\n";
    std::cout << "  Instructions Retired:  " << s.instructions_retired << "\n";
    std::cout << "  Total Clock Cycles:    " << s.total_cycles << "\n";
    std::cout << "  Clock Period:          " << std::fixed << std::setprecision(2) << s.clock_period_ns << " ns\n";
    std::cout << "  Total Execution Time:  " << formatTime(s.total_time_ns) << "\n";
    std::cout << "  Stall Cycles:          " << s.stall_cycles << "\n";
    if (s.total_cycles > 0) {
        double stall_pct = (static_cast<double>(s.stall_cycles) / s.total_cycles) * 100.0;
        std::cout << "  Stall Overhead:        " << std::fixed << std::setprecision(1) << stall_pct << "%\n";
    }
    std::cout << "  CPI:                   " << std::fixed << std::setprecision(2) << s.cpi << "\n";
    std::cout << "  Status:                " << (s.completed ? "Halted / Finished" : "Running / Safety Limit") << "\n";
    std::cout << "=======================================================\n\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> loaded_binary;
    std::string current_file;
    PipelineStats last_stats{};
    bool has_last_stats = false;
    SessionManager sessions;

    // Check command line arguments for batch / direct mode
    if (argc >= 2) {
        std::string arg1 = argv[1];
        if (arg1 == "--help" || arg1 == "-h") {
            printHelp();
            return 0;
        }

        std::string filename;
        std::string mode = "compare";

        if (arg1 == "--4stage" || arg1 == "-4") {
            mode = "run4";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--6stage" || arg1 == "-6") {
            mode = "run6";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--debug4" || arg1 == "-d4") {
            mode = "debug4";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--debug6" || arg1 == "-d6") {
            mode = "debug6";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--tui4" || arg1 == "-t4") {
            mode = "tui4";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--tui6" || arg1 == "-t6") {
            mode = "tui6";
            if (argc >= 3) filename = argv[2];
        } else if (arg1 == "--tui") {
            mode = "tui6";
            if (argc >= 3) filename = argv[2];
        } else {
            filename = arg1;
        }

        if (!filename.empty()) {
            std::string err;
            if (!loadProgramAuto(filename, loaded_binary, err)) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            std::cout << "Loaded " << loaded_binary.size() << " instructions from " << filename << "\n";

            if (mode == "run4") {
                PipelineStats s = runFourStage(loaded_binary, false);
                printStats(s);
                return 0;
            } else if (mode == "run6") {
                PipelineStats s = runSixStage(loaded_binary, false);
                printStats(s);
                return 0;
            } else if (mode == "debug4") {
                runFourStage(loaded_binary, true);
                return 0;
            } else if (mode == "debug6") {
                runSixStage(loaded_binary, true);
                return 0;
            } else if (mode == "tui4" || mode == "tui6") {
                const bool opened = (mode == "tui4") ? runTuiFourStage(loaded_binary)
                                                     : runTuiSixStage(loaded_binary);
                if (!opened) {
                    std::cerr << "TUI needs an interactive terminal; "
                                 "use --debug4/--debug6 instead.\n";
                    return 1;
                }
                return 0;
            } else {
                PipelineStats s4 = runFourStage(loaded_binary, false);
                PipelineStats s6 = runSixStage(loaded_binary, false);
                printComparison(s4, s6);
                return 0;
            }
        }
    }

    printBanner();

    std::string line;
    while (true) {
        std::cout << "risc201> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "help" || cmd == "h") {
            printHelp();
        } else if (cmd == "load") {
            std::string path;
            if (!(iss >> path)) {
                std::cout << "Usage: load <file.s | file.bin>\n";
                continue;
            }
            std::string err;
            std::vector<std::string> bin;
            if (!loadProgramAuto(path, bin, err)) {
                std::cout << "Error: " << err << "\n";
            } else {
                loaded_binary = bin;
                current_file = path;
                sessions.invalidate();
                std::cout << "Successfully loaded " << loaded_binary.size()
                          << " instructions from '" << path << "'.\n";
            }
        } else if (cmd == "assemble") {
            std::string in_path, out_path;
            if (!(iss >> in_path)) {
                std::cout << "Usage: assemble <input.s> [output.bin]\n";
                continue;
            }
            if (!(iss >> out_path)) {
                out_path = in_path + ".bin";
            }
            std::string err;
            std::vector<std::string> bin;
            if (!assembleSource(in_path, bin, err)) {
                std::cout << "Error: " << err << "\n";
            } else {
                std::ofstream outfile(out_path);
                if (!outfile.is_open()) {
                    std::cout << "Error: Could not write to " << out_path << "\n";
                } else {
                    for (const auto& b : bin) {
                        outfile << b << "\n";
                    }
                    std::cout << "Assembled " << bin.size() << " instructions into '"
                              << out_path << "'.\n";
                }
            }
        } else if (cmd == "run4") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            last_stats = runFourStage(loaded_binary, false);
            has_last_stats = true;
            printStats(last_stats);
        } else if (cmd == "run6") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            last_stats = runSixStage(loaded_binary, false);
            has_last_stats = true;
            printStats(last_stats);
        } else if (cmd == "debug4") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            runFourStage(loaded_binary, true);
        } else if (cmd == "debug6") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            runSixStage(loaded_binary, true);
        } else if (cmd == "tui4" || cmd == "tui6" || cmd == "tui") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            bool four = (cmd == "tui4");
            if (cmd == "tui") {
                std::string width;
                if (iss >> width && width == "4") four = true;
            }
            const bool opened = four ? runTuiFourStage(loaded_binary)
                                     : runTuiSixStage(loaded_binary);
            if (!opened) {
                std::cout << "TUI needs an interactive terminal; "
                             "use debug4 / debug6 instead.\n";
            }
        } else if (cmd == "compare") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }
            std::cout << "Running on 4-stage pipeline...\n";
            PipelineStats s4 = runFourStage(loaded_binary, false);
            std::cout << "Running on 6-stage pipeline...\n";
            PipelineStats s6 = runSixStage(loaded_binary, false);
            printComparison(s4, s6);
        } else if (cmd == "pipeline" || cmd == "micropipeline") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded. Use 'load <file.s>' first.\n";
                continue;
            }

            const bool micro = (cmd == "micropipeline");
            bool four = micro ? sessions.microIsFour() : sessions.romIsFour();
            uint64_t cycles = 0;
            bool do_run = false;
            bool do_reset = false;
            bool full_listing = false;
            bool bad_option = false;
            bool width_taken = false;

            std::string token;
            while (iss >> token) {
                if (!width_taken && token == "4") {
                    four = true;
                    width_taken = true;
                } else if (!width_taken && token == "6") {
                    four = false;
                    width_taken = true;
                } else if (token == "run") {
                    do_run = true;
                } else if (token == "reset") {
                    do_reset = true;
                } else if (token == "all") {
                    full_listing = true;
                } else if (!parseCycleCount(token, cycles)) {
                    std::cout << "Unknown option '" << token
                              << "'. Use 4|6, a cycle count, run, reset, or all.\n";
                    bad_option = true;
                    break;
                }
            }
            if (bad_option) continue;

            EngineSession& session = micro
                ? sessions.micro(four, loaded_binary)
                : sessions.controlRom(four, loaded_binary);

            if (do_reset) session.reset();
            if (do_run) {
                session.run();
            } else {
                for (uint64_t i = 0; i < cycles; ++i) {
                    if (!session.step()) break;
                }
            }
            session.printView(std::cout, full_listing);
        } else if (cmd == "program" || cmd == "listing") {
            if (loaded_binary.empty()) {
                std::cout << "No program loaded.\n";
            } else {
                std::cout << "Loaded Program (" << loaded_binary.size() << " instructions from "
                          << current_file << "):\n";
                for (size_t i = 0; i < loaded_binary.size(); ++i) {
                    std::cout << "  [" << std::setw(3) << i << "] " << loaded_binary[i] << "\n";
                }
            }
        } else if (cmd == "stats") {
            if (!has_last_stats) {
                std::cout << "No execution stats available. Run a program first with 'run4' or 'run6'.\n";
            } else {
                printStats(last_stats);
            }
        } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            std::cout << "Exiting RISC201 simulator. Goodbye!\n";
            break;
        } else {
            std::cout << "Unknown command: '" << cmd << "'. Type 'help' for commands.\n";
        }
    }

    return 0;
}
