#include "disassembler.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* executable) {
    std::cout
        << "RISC201 Disassembler\n\n"
        << "Usage: " << executable << " <input.bin> <output.s> [options]\n\n"
        << "Options:\n"
        << "  --aliases       Use zero/fp/sp/ra and f0-f3 register aliases\n"
        << "  --no-labels     Do not generate labels for in-range branch targets\n"
        << "  --addresses     Add byte addresses as assembly comments\n"
        << "  --words         Add original 32-bit words as comments\n"
        << "  --raw-le        Read raw 4-byte little-endian machine words instead\n"
        << "  --help          Show this help message\n\n"
        << "Default input: one 32-bit binary word per line (the existing assembler's .bin format).\n"
        << "Also accepted: 0x-prefixed 8-digit hexadecimal words.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "--help" ||
                      std::string(argv[1]) == "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    risc201::DisassemblerOptions options;
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--aliases") options.use_register_aliases = true;
        else if (option == "--no-labels") options.generate_labels = false;
        else if (option == "--addresses") options.show_addresses = true;
        else if (option == "--words") options.show_machine_words = true;
        else if (option == "--raw-le") options.input_format = risc201::InputFormat::RawLittleEndian;
        else if (option == "--help" || option == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << option << "\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    risc201::Disassembler disassembler;
    std::string error;
    if (!disassembler.disassembleFile(argv[1], argv[2], options, error)) {
        std::cerr << "Disassembly failed: " << error << '\n';
        return 1;
    }

    std::cout << "Disassembly generated successfully: " << argv[2] << '\n';
    return 0;
}
