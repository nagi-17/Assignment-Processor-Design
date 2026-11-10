#include "instruction_text.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

#include "isa_config.hpp"

namespace tui {
namespace {

constexpr std::uint32_t kOpcodeShift = 26;
constexpr std::uint32_t kFormatShift = 25;

const std::unordered_map<std::uint32_t, std::string>& reverseOpcodes() {
    static const std::unordered_map<std::uint32_t, std::string> map = [] {
        std::unordered_map<std::uint32_t, std::string> result;
        for (const auto& entry : OPCODE_MAP) {
            if (entry.second.size() != 6) continue;
            std::uint32_t value = 0;
            for (char bit : entry.second) {
                value = (value << 1) | static_cast<std::uint32_t>(bit == '1' ? 1 : 0);
            }
            result.emplace(value, entry.first);
        }
        return result;
    }();
    return map;
}

std::uint32_t opcodeOf(std::uint32_t word) {
    return (word >> kOpcodeShift) & 0x3Fu;
}

bool formatIsImmediate(std::uint32_t word) {
    return ((word >> kFormatShift) & 1u) != 0;
}

unsigned rdOf(std::uint32_t word)  { return (word >> 21) & 0xFu; }
unsigned rs1Of(std::uint32_t word) { return (word >> 17) & 0xFu; }
unsigned rs2Of(std::uint32_t word) { return (word >> 13) & 0xFu; }

std::int32_t signExtend16(std::uint32_t value) {
    const std::uint32_t bits = value & 0xFFFFu;
    return (bits & 0x8000u) ? static_cast<std::int32_t>(bits) - 0x10000
                            : static_cast<std::int32_t>(bits);
}

// Branch/call fields carry a 25-bit signed quantity.
std::int32_t signed25(std::uint32_t value) {
    const std::uint32_t bits = value & 0x1FFFFFFu;
    return (bits & 0x1000000u) ? static_cast<std::int32_t>(bits) - 0x2000000
                               : static_cast<std::int32_t>(bits);
}

bool isIntegerAlu(const std::string& mnemonic) {
    static const std::vector<std::string> names = {
        "add", "sub", "mul", "div", "mod", "cmp", "and", "or",
        "xor", "not", "lsl", "lsr", "asr"
    };
    for (const std::string& name : names) if (name == mnemonic) return true;
    return false;
}

bool isFloatAlu(const std::string& mnemonic) {
    return mnemonic == "fadd" || mnemonic == "fsub" || mnemonic == "fmul" ||
           mnemonic == "fdiv" || mnemonic == "fcmp";
}

bool isBranch(const std::string& mnemonic) {
    return mnemonic == "b" || mnemonic == "beq" || mnemonic == "bne" ||
           mnemonic == "bgt" || mnemonic == "blt" || mnemonic == "call";
}

bool isSystem(const std::string& mnemonic) {
    return mnemonic == "nop" || mnemonic == "ret" || mnemonic == "ecall" ||
           mnemonic == "eret" || mnemonic == "ebreak" || mnemonic == "wfi";
}

const DebugField* findField(const std::vector<DebugField>& fields, const char* name) {
    for (const DebugField& field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

bool fieldSet(const std::vector<DebugField>& fields, const char* name) {
    const DebugField* field = findField(fields, name);
    return field != nullptr && field->value != 0;
}

} // namespace

std::string registerName(unsigned index, bool floating) {
    if (index == 12) return "zero";
    if (index == 13) return "fp";
    if (index == 14) return "sp";
    if (index == 15) return "ra";
    if (floating && index >= 8 && index <= 11) return "f" + std::to_string(index - 8);
    return "r" + std::to_string(index);
}

std::string decodeInstruction(std::uint32_t word) {
    const auto it = reverseOpcodes().find(opcodeOf(word));
    if (it == reverseOpcodes().end()) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), ".word 0x%08X", word);
        return buffer;
    }

    const std::string& mnemonic = it->second;
    const bool immediate = formatIsImmediate(word);

    if (isSystem(mnemonic)) return mnemonic;

    if (isBranch(mnemonic)) {
        const std::int32_t offset = signed25(word & 0x1FFFFFFu);
        if (mnemonic == "b" || mnemonic == "call") return mnemonic + " " + std::to_string(offset);
        return mnemonic + " " + registerName(rs1Of(word)) + ", " + std::to_string(offset);
    }

    if (mnemonic == "ld" || mnemonic == "st") {
        const std::int32_t offset = signExtend16((word >> 1) & 0xFFFFu);
        return mnemonic + " " + registerName(rdOf(word)) + ", " +
               registerName(rs1Of(word)) + ", " + std::to_string(offset);
    }

    if (mnemonic == "movu" || mnemonic == "movh" || mnemonic == "csrr") {
        return mnemonic + " " + registerName(rdOf(word)) + ", " +
               std::to_string((word >> 1) & 0xFFFFu);
    }

    if (mnemonic == "csrw") {
        return "csrw " + std::to_string((word >> 1) & 0xFFFFu) + ", " +
               registerName(rs1Of(word));
    }

    if (mnemonic == "clrz") return "clrz " + registerName(rdOf(word));

    if (mnemonic == "chk")
        return "chk " + registerName(rs1Of(word)) + ", " + registerName(rs2Of(word));

    if (isFloatAlu(mnemonic)) {
        return mnemonic + " " + registerName(rdOf(word), true) + ", " +
               registerName(rs1Of(word), true) + ", " +
               registerName(rs2Of(word), true);
    }

    if (isIntegerAlu(mnemonic)) {
        if (immediate) {
            const std::int32_t value = signExtend16((word >> 1) & 0xFFFFu);
            return mnemonic + " " + registerName(rdOf(word)) + ", " +
                   registerName(rs1Of(word)) + ", " + std::to_string(value);
        }
        return mnemonic + " " + registerName(rdOf(word)) + ", " +
               registerName(rs1Of(word)) + ", " + registerName(rs2Of(word));
    }

    return mnemonic;
}

std::string mnemonicFromControlFields(const std::vector<DebugField>& fields) {
    struct Map { const char* signal; const char* mnemonic; };
    static const Map map[] = {
        {"IS_ST", "st"},   {"IS_LD", "ld"},     {"IS_BEQ", "beq"},
        {"IS_BGT", "bgt"}, {"IS_BLT", "blt"},   {"IS_BNE", "bne"},
        {"IS_RET", "ret"}, {"IS_CALL", "call"}, {"IS_MOVU", "movu"},
        {"IS_MOVH", "movh"}, {"IS_ADD", "add"}, {"IS_SUB", "sub"},
        {"IS_CMP", "cmp"}, {"IS_MUL", "mul"},   {"IS_DIV", "div"},
        {"IS_MOD", "mod"}, {"IS_LSL", "lsl"},   {"IS_LSR", "lsr"},
        {"IS_ASR", "asr"}, {"IS_OR", "or"},     {"IS_AND", "and"},
        {"IS_NOT", "not"}, {"IS_XOR", "xor"},   {"IS_WFI", "wfi"},
        {"IS_ERET", "eret"}, {"IS_CSRR", "csrr"}, {"IS_CSRW", "csrw"},
        {"IS_SYSCALL", "ecall"}, {"IS_EBREAK", "ebreak"},
        {"IS_CLEAR", "clrz"}, {"IS_CHK", "chk"},
        {"IS_FADD", "fadd"}, {"IS_FSUB", "fsub"}, {"IS_FMUL", "fmul"},
        {"IS_FDIV", "fdiv"}, {"IS_FCMP", "fcmp"},
    };

    for (const Map& entry : map) {
        if (fieldSet(fields, entry.signal)) return entry.mnemonic;
    }

    // A float writeback with no explicit FPU op is fmov.
    if (fieldSet(fields, "IS_FLOAT")) return "fmov";

    return {};
}

std::string stageInstructionText(const PipelineStageSnapshot& stage,
                                 const std::vector<std::string>& program) {
    if (!stage.is_valid) return "-";

    if (const DebugField* ir = findField(stage.fields, "IR")) {
        if (ir->value != 0) return decodeInstruction(static_cast<std::uint32_t>(ir->value));
    }

    if (const DebugField* pc = findField(stage.fields, "PC_NEXT")) {
        const std::uint64_t index = pc->value;
        if (index > 0 && index <= program.size()) {
            std::uint32_t word = 0;
            if (parseProgramWord(program[static_cast<std::size_t>(index - 1)], word))
                return decodeInstruction(word);
        }
    }

    const std::string mnemonic = mnemonicFromControlFields(stage.fields);
    if (!mnemonic.empty()) return mnemonic;

    return "-";
}

int stageDestRegister(const PipelineStageSnapshot& stage) {
    const DebugField* rd = findField(stage.fields, "RD");
    if (rd == nullptr) return -1;
    return static_cast<int>(rd->value);
}

bool parseProgramWord(const std::string& text, std::uint32_t& word) {
    word = 0;
    if (text.size() == 32) {
        bool binary = true;
        for (char c : text) {
            if (c != '0' && c != '1') { binary = false; break; }
        }
        if (binary) {
            for (char c : text) word = (word << 1) | static_cast<std::uint32_t>(c - '0');
            return true;
        }
    }

    std::string hex = text;
    if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex = hex.substr(2);
    if (hex.empty() || hex.size() > 8) return false;
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }

    std::uint32_t value = 0;
    for (char c : hex) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
        else value |= static_cast<std::uint32_t>(std::tolower(c) - 'a' + 10);
    }
    word = value;
    return true;
}

std::vector<std::uint32_t> parseProgramImage(const std::vector<std::string>& program) {
    std::vector<std::uint32_t> words;
    words.reserve(program.size());
    for (const std::string& line : program) {
        std::uint32_t word = 0;
        words.push_back(parseProgramWord(line, word) ? word : 0u);
    }
    return words;
}

} // namespace tui
