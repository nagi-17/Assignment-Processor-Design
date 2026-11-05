#include "disassembler.hpp"

#include "../headers/isa_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace risc201 {
namespace {

constexpr uint32_t kOpcodeShift = 26;
constexpr uint32_t kFormatShift = 25;
constexpr uint32_t kRdShift = 21;
constexpr uint32_t kRs1Shift = 17;
constexpr uint32_t kRs2Shift = 13;
constexpr uint32_t kImmediateShift = 1;
constexpr uint32_t kImmediateMask = 0xFFFFu;
constexpr uint32_t kLowPaddingMask = 0x1u;
constexpr uint32_t kRPaddingMask = 0x1FFFu;

const std::set<std::string>& integerAluMnemonics() {
    static const std::set<std::string> names = {
        "add", "sub", "mul", "div", "mod", "cmp", "and", "or",
        "xor", "not", "lsl", "lsr", "asr"
    };
    return names;
}

const std::set<std::string>& branchMnemonics() {
    static const std::set<std::string> names = {
        "b", "beq", "bne", "bgt", "blt", "call"
    };
    return names;
}

bool isConditionalBranch(const std::string& mnemonic) {
    return mnemonic == "beq" || mnemonic == "bne" ||
           mnemonic == "bgt" || mnemonic == "blt";
}

const std::set<std::string>& systemMnemonics() {
    static const std::set<std::string> names = {
        "nop", "ret", "ecall", "eret", "ebreak", "wfi"
    };
    return names;
}

const std::set<std::string>& floatingMnemonics() {
    static const std::set<std::string> names = {
        "fadd", "fsub", "fmul", "fdiv", "fcmp"
    };
    return names;
}

const std::unordered_map<std::string, std::string>& reverseOpcodeMap() {
    static const std::unordered_map<std::string, std::string> reverse = [] {
        std::unordered_map<std::string, std::string> result;
        for (const auto& entry : OPCODE_MAP) {
            if (entry.second.size() == 6) {
                result.emplace(entry.second, entry.first);
            }
        }
        return result;
    }();
    return reverse;
}

std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8)
        << std::setfill('0') << value;
    return out.str();
}

std::string labelFor(uint32_t address) {
    std::ostringstream out;
    out << "L_" << std::uppercase << std::hex << std::setw(8)
        << std::setfill('0') << address;
    return out.str();
}

uint8_t opcodeOf(uint32_t word) {
    return static_cast<uint8_t>((word >> kOpcodeShift) & 0x3Fu);
}

bool formatIsImmediate(uint32_t word) {
    return ((word >> kFormatShift) & 1u) != 0;
}

uint8_t rdOf(uint32_t word) {
    return static_cast<uint8_t>((word >> kRdShift) & 0xFu);
}

uint8_t rs1Of(uint32_t word) {
    return static_cast<uint8_t>((word >> kRs1Shift) & 0xFu);
}

uint8_t rs2Of(uint32_t word) {
    return static_cast<uint8_t>((word >> kRs2Shift) & 0xFu);
}

uint16_t immediateBitsOf(uint32_t word) {
    // The assembler puts imm[15:0] in instruction bits [16:1].
    return static_cast<uint16_t>((word >> kImmediateShift) & kImmediateMask);
}

int32_t signExtend16(uint16_t value) {
    return (value & 0x8000u)
        ? static_cast<int32_t>(value) - 0x10000
        : static_cast<int32_t>(value);
}

bool hasZeroLowPaddingBit(uint32_t word) {
    return (word & kLowPaddingMask) == 0;
}

bool hasZeroRPadding(uint32_t word) {
    return (word & kRPaddingMask) == 0;
}

bool hasZeroSystemPayload(uint32_t word) {
    return (word & 0x03FFFFFFu) == 0;
}

std::string reg(unsigned index, const DisassemblerOptions& options,
                bool floating = false) {
    return Disassembler::registerName(index, options.use_register_aliases, floating);
}

struct Decoded {
    std::string assembly;
    std::string mnemonic;
    bool valid{false};
    bool branch{false};
    int64_t branch_target{0};
    std::string diagnostic;
};

Decoded invalidWord(uint32_t word, const std::string& why) {
    Decoded result;
    result.valid = false;
    result.assembly = ".word " + hex32(word) + " ; " + why;
    result.diagnostic = why;
    return result;
}

Decoded decodeOne(uint32_t word, uint32_t address,
                  const DisassemblerOptions& options) {
    const std::string opcodeBits = [&] {
        std::string bits;
        for (int bit = 5; bit >= 0; --bit)
            bits.push_back(((opcodeOf(word) >> bit) & 1u) ? '1' : '0');
        return bits;
    }();

    const auto opcodeIt = reverseOpcodeMap().find(opcodeBits);
    if (opcodeIt == reverseOpcodeMap().end())
        return invalidWord(word, "unknown opcode");

    const std::string& mnemonic = opcodeIt->second;
    const bool immediateFormat = formatIsImmediate(word);
    const uint8_t rd = rdOf(word);
    const uint8_t rs1 = rs1Of(word);
    const uint8_t rs2 = rs2Of(word);
    const uint16_t immBits = immediateBitsOf(word);
    const int32_t signedImm = signExtend16(immBits);

    Decoded result;
    result.valid = true;
    result.mnemonic = mnemonic;

    // Pseudo-instruction fmov is expanded by the existing preprocessor into
    // movu + movh. Its declared F_TYPE has no BinaryGenerator implementation,
    // and the MCU's fmov format-bit convention disagrees with the README.
    if (mnemonic == "fmov")
        return invalidWord(word, "fmov has no defined emitted machine format; normal source expands it to movu/movh");

    if (systemMnemonics().count(mnemonic) != 0) {
        if (immediateFormat || !hasZeroSystemPayload(word))
            return invalidWord(word, "invalid system-instruction format or non-zero reserved bits");
        result.assembly = mnemonic;
        return result;
    }

    if (branchMnemonics().count(mnemonic) != 0) {
        if (!immediateFormat || rd != 0 || !hasZeroLowPaddingBit(word))
            return invalidWord(word, "invalid branch format or non-zero reserved bits");
        if ((mnemonic == "b" || mnemonic == "call") && rs1 != 0)
            return invalidWord(word, "unconditional branch has non-zero reserved register field");

        // The assembler stores (target byte address - current byte address)
        // as a signed 16-bit displacement in bits [16:1].
        const int32_t displacement = signedImm;
        result.branch = true;
        result.branch_target = static_cast<int64_t>(address) + displacement;
        if (mnemonic == "b" || mnemonic == "call") {
            result.assembly = mnemonic + " " + labelFor(
                result.branch_target >= 0 && result.branch_target <= UINT32_MAX
                    ? static_cast<uint32_t>(result.branch_target) : 0u);
        } else {
            // BinaryGenerator drops conditional branch operand 2. r12 is a
            // canonical filler: it reproduces the ignored field and the same
            // 32-bit word when reassembled.
            result.assembly = mnemonic + " " + reg(rs1, options) + ", " +
                              reg(12, options) + ", " + labelFor(
                result.branch_target >= 0 && result.branch_target <= UINT32_MAX
                    ? static_cast<uint32_t>(result.branch_target) : 0u);
        }
        return result;
    }

    if (mnemonic == "ld" || mnemonic == "st") {
        if (!immediateFormat || !hasZeroLowPaddingBit(word))
            return invalidWord(word, "invalid memory-instruction format or non-zero padding bit");
        result.assembly = mnemonic + " " + reg(rd, options) + ", " +
                          reg(rs1, options) + ", " + std::to_string(signedImm);
        return result;
    }

    if (mnemonic == "movu" || mnemonic == "movh" || mnemonic == "csrr") {
        if (!immediateFormat || rs1 != 0 || !hasZeroLowPaddingBit(word))
            return invalidWord(word, "invalid two-field immediate format or non-zero reserved bits");
        result.assembly = mnemonic + " " + reg(rd, options) + ", " +
                          std::to_string(static_cast<uint32_t>(immBits));
        return result;
    }

    if (mnemonic == "csrw") {
        if (!immediateFormat || rd != 0 || !hasZeroLowPaddingBit(word))
            return invalidWord(word, "invalid csrw format or non-zero reserved bits");
        result.assembly = "csrw " + std::to_string(static_cast<uint32_t>(immBits)) +
                          ", " + reg(rs1, options);
        return result;
    }

    if (mnemonic == "clrz") {
        if (immediateFormat || rs1 != 0 || rs2 != 0 || !hasZeroRPadding(word))
            return invalidWord(word, "invalid clrz format or non-zero reserved bits");
        result.assembly = "clrz " + reg(rd, options);
        return result;
    }

    if (mnemonic == "fcmp") {
        if (immediateFormat || rd != 0 || !hasZeroRPadding(word))
            return invalidWord(word, "invalid fcmp format or non-zero reserved bits");
        result.assembly = "fcmp " + reg(rs1, options, true) + ", " +
                          reg(rs2, options, true);
        return result;
    }

    if (mnemonic == "chk") {
        if (immediateFormat || rd != 0 || !hasZeroRPadding(word))
            return invalidWord(word, "invalid chk format or non-zero reserved bits");
        result.assembly = "chk " + reg(rs1, options) + ", " + reg(rs2, options);
        return result;
    }

    if (floatingMnemonics().count(mnemonic) != 0) {
        if (immediateFormat || !hasZeroRPadding(word))
            return invalidWord(word, "invalid floating-point register format or non-zero padding");
        result.assembly = mnemonic + " " + reg(rd, options, true) + ", " +
                          reg(rs1, options, true) + ", " + reg(rs2, options, true);
        return result;
    }

    if (integerAluMnemonics().count(mnemonic) != 0) {
        if (immediateFormat) {
            if (!hasZeroLowPaddingBit(word))
                return invalidWord(word, "immediate instruction has a non-zero padding bit");
            // Canonical three-operand spelling also represents the assembler's
            // two-operand I_TYPE_2 form: that form simply has rs1 = r0.
            result.assembly = mnemonic + " " + reg(rd, options) + ", " +
                              reg(rs1, options) + ", " + std::to_string(signedImm);
        } else {
            if (!hasZeroRPadding(word))
                return invalidWord(word, "register instruction has non-zero padding bits");
            // Canonical three-operand spelling also represents R_TYPE_2:
            // its rd field is zero, so this spelling re-encodes identically.
            result.assembly = mnemonic + " " + reg(rd, options) + ", " +
                              reg(rs1, options) + ", " + reg(rs2, options);
        }
        return result;
    }

    return invalidWord(word, "opcode exists in ISA map but has no disassembler format");
}

bool parseRawLittleEndian(std::istream& input, std::vector<uint32_t>& words,
                          std::string& error) {
    std::vector<unsigned char> bytes;
    char byte = 0;
    while (input.get(byte))
        bytes.push_back(static_cast<unsigned char>(byte));

    if (bytes.size() % 4 != 0) {
        error = "raw little-endian input size is not a multiple of four bytes";
        return false;
    }

    words.reserve(bytes.size() / 4);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const uint32_t word = static_cast<uint32_t>(bytes[i]) |
            (static_cast<uint32_t>(bytes[i + 1]) << 8) |
            (static_cast<uint32_t>(bytes[i + 2]) << 16) |
            (static_cast<uint32_t>(bytes[i + 3]) << 24);
        words.push_back(word);
    }
    return true;
}

bool parseTextWords(std::istream& input, std::vector<uint32_t>& words,
                    std::string& error) {
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream tokens(line);
        std::string token;
        while (tokens >> token) {
            uint32_t word = 0;
            std::string tokenError;
            if (!Disassembler::parseWordToken(token, word, tokenError)) {
                error = "line " + std::to_string(lineNumber) + ": " + tokenError;
                return false;
            }
            words.push_back(word);
        }
    }
    if (input.bad()) {
        error = "I/O error while reading machine-code input";
        return false;
    }
    if (words.empty()) {
        error = "input contains no machine-code words";
        return false;
    }
    return true;
}

} // namespace

std::string Disassembler::registerName(unsigned index, bool use_aliases,
                                       bool floating_register) {
    if (index > 15) return "r?";
    if (floating_register && use_aliases && index >= 8 && index <= 11)
        return "f" + std::to_string(index - 8);
    if (use_aliases) {
        switch (index) {
            case 12: return "zero";
            case 13: return "fp";
            case 14: return "sp";
            case 15: return "ra";
            default: break;
        }
    }
    return "r" + std::to_string(index);
}

bool Disassembler::parseWordToken(const std::string& original,
                                  uint32_t& word,
                                  std::string& error) {
    error.clear();
    const std::string token = trim(original);
    if (token.empty()) {
        error = "empty machine-code token";
        return false;
    }

    if (token.size() == 32 &&
        std::all_of(token.begin(), token.end(), [](char c) { return c == '0' || c == '1'; })) {
        uint32_t value = 0;
        for (char bit : token) value = (value << 1) | static_cast<uint32_t>(bit - '0');
        word = value;
        return true;
    }

    std::string hexToken = token;
    if (hexToken.size() >= 2 && hexToken[0] == '0' &&
        (hexToken[1] == 'x' || hexToken[1] == 'X'))
        hexToken.erase(0, 2);

    if (hexToken.size() == 8 &&
        std::all_of(hexToken.begin(), hexToken.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        })) {
        try {
            std::size_t consumed = 0;
            const unsigned long parsed = std::stoul(hexToken, &consumed, 16);
            if (consumed != hexToken.size() || parsed > 0xFFFFFFFFul) {
                error = "hexadecimal word is outside the 32-bit range: " + token;
                return false;
            }
            word = static_cast<uint32_t>(parsed);
            return true;
        } catch (const std::exception&) {
            error = "invalid hexadecimal machine-code word: " + token;
            return false;
        }
    }

    error = "expected exactly 32 binary digits or an 8-digit hexadecimal word, got: " + token;
    return false;
}

bool Disassembler::disassemble(std::istream& input,
                               std::ostream& output,
                               const DisassemblerOptions& options,
                               std::string& error) const {
    error.clear();
    std::vector<uint32_t> words;
    const bool parsed = options.input_format == InputFormat::RawLittleEndian
        ? parseRawLittleEndian(input, words, error)
        : parseTextWords(input, words, error);
    if (!parsed) return false;
    if (words.empty()) {
        error = "input contains no machine-code words";
        return false;
    }

    std::vector<Decoded> decoded;
    decoded.reserve(words.size());
    const uint64_t imageEnd64 = static_cast<uint64_t>(words.size()) * 4u;
    if (imageEnd64 > std::numeric_limits<uint32_t>::max()) {
        error = "input is too large for the RISC201 32-bit address space";
        return false;
    }
    const uint32_t imageEnd = static_cast<uint32_t>(imageEnd64);

    std::map<uint32_t, std::string> labels;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const uint32_t address = static_cast<uint32_t>(i * 4u);
        decoded.push_back(decodeOne(words[i], address, options));
        const Decoded& current = decoded.back();
        if (options.generate_labels && current.branch && current.branch_target >= 0 &&
            current.branch_target <= imageEnd && (current.branch_target % 4) == 0) {
            const uint32_t target = static_cast<uint32_t>(current.branch_target);
            labels.emplace(target, labelFor(target));
        }
    }

    output << "; RISC201 disassembly generated from machine code.\n"
           << "; Addresses are byte addresses; labels are synthetic.\n";

    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const uint32_t address = static_cast<uint32_t>(i * 4u);
        const auto label = labels.find(address);
        if (label != labels.end()) output << label->second << ":\n";

        std::string assembly = decoded[i].assembly;
        if (decoded[i].branch) {
            const int64_t target64 = decoded[i].branch_target;
            const bool targetInImage = target64 >= 0 && target64 <= imageEnd &&
                                       (target64 % 4) == 0;
            assembly = decoded[i].mnemonic;
            if (isConditionalBranch(decoded[i].mnemonic)) {
                assembly += " " + reg(rs1Of(words[i]), options) + ", " +
                            reg(12, options) + ", ";
            } else {
                assembly += " ";
            }

            if (targetInImage && options.generate_labels) {
                assembly += labelFor(static_cast<uint32_t>(target64));
            } else {
                // The architecture has a 32-bit PC, so represent an address
                // outside the image as its 32-bit wrapped absolute value.
                assembly += hex32(static_cast<uint32_t>(target64));
                if (!targetInImage)
                    assembly += " ; unresolved target outside this image or unaligned";
                else
                    assembly += " ; absolute target address (labels disabled)";
            }
        }

        // Put comments after the instruction so that the assembler's
        // comment-stripping preprocessor does not accidentally erase it.
        output << assembly;
        if (options.show_addresses)
            output << " ; address=" << hex32(address);
        if (options.show_machine_words)
            output << " ; word=" << hex32(words[i]);
        output << '\n';
    }

    const auto endLabel = labels.find(imageEnd);
    if (endLabel != labels.end()) output << endLabel->second << ":\n";
    if (!output) {
        error = "I/O error while writing disassembly output";
        return false;
    }
    return true;
}

bool Disassembler::disassembleFile(const std::string& input_path,
                                   const std::string& output_path,
                                   const DisassemblerOptions& options,
                                   std::string& error) const {
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        error = "could not open input file: " + input_path;
        return false;
    }

    // Render completely before opening the destination. This avoids deleting
    // an existing output file on malformed input and also supports using the
    // same path for input and output without truncating the source too early.
    std::ostringstream rendered;
    if (!disassemble(input, rendered, options, error))
        return false;

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "could not create output file: " + output_path;
        return false;
    }

    const std::string contents = rendered.str();
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        error = "I/O error while writing disassembly output: " + output_path;
        return false;
    }
    return true;
}

} // namespace risc201
