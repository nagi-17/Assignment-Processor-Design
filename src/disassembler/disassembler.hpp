#ifndef RISC201_DISASSEMBLER_HPP
#define RISC201_DISASSEMBLER_HPP

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace risc201 {

enum class InputFormat {
    TextWords,       // One 32-bit binary word (or 0x-prefixed hex word) per token.
    RawLittleEndian  // Four bytes per instruction, least-significant byte first.
};

struct DisassemblerOptions {
    bool generate_labels{true};
    bool use_register_aliases{false};
    bool show_addresses{false};
    bool show_machine_words{false};
    InputFormat input_format{InputFormat::TextWords};
};

class Disassembler {
public:
    // Reads the project's assembler output format by default. The output is
    // canonical RISC201 assembly; generated labels are based on byte addresses.
    bool disassemble(std::istream& input,
                     std::ostream& output,
                     const DisassemblerOptions& options,
                     std::string& error) const;

    bool disassembleFile(const std::string& input_path,
                         const std::string& output_path,
                         const DisassemblerOptions& options,
                         std::string& error) const;

    // Parses one textual instruction word: exactly 32 binary digits, or an
    // eight-digit hexadecimal word with an optional 0x prefix.
    static bool parseWordToken(const std::string& token,
                               uint32_t& word,
                               std::string& error);

    static std::string registerName(unsigned index,
                                    bool use_aliases = false,
                                    bool floating_register = false);
};

} // namespace risc201

#endif // RISC201_DISASSEMBLER_HPP
