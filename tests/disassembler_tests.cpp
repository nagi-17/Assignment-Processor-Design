#include "../src/disassembler/disassembler.hpp"
#include "../src/headers/isa_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

uint32_t opcode(const std::string& mnemonic) {
    return static_cast<uint32_t>(std::stoul(OPCODE_MAP.at(mnemonic), nullptr, 2));
}

uint32_t r3(const std::string& mnemonic, unsigned rd, unsigned rs1, unsigned rs2) {
    return (opcode(mnemonic) << 26) | (rd << 21) | (rs1 << 17) | (rs2 << 13);
}

uint32_t r2(const std::string& mnemonic, unsigned rs1, unsigned rs2) {
    return (opcode(mnemonic) << 26) | (rs1 << 17) | (rs2 << 13);
}

uint32_t i3(const std::string& mnemonic, unsigned rd, unsigned rs1, int imm) {
    return (opcode(mnemonic) << 26) | (1u << 25) | (rd << 21) |
           (rs1 << 17) | (static_cast<uint32_t>(static_cast<uint16_t>(imm)) << 1);
}

uint32_t i2(const std::string& mnemonic, unsigned rd, unsigned imm) {
    return (opcode(mnemonic) << 26) | (1u << 25) | (rd << 21) |
           (static_cast<uint32_t>(static_cast<uint16_t>(imm)) << 1);
}

uint32_t branch(const std::string& mnemonic, int byteDisplacement, unsigned conditionRegister = 0) {
    return (opcode(mnemonic) << 26) | (1u << 25) | (conditionRegister << 17) |
           (static_cast<uint32_t>(static_cast<uint16_t>(byteDisplacement)) << 1);
}

std::string bits(uint32_t word) {
    std::string result(32, '0');
    for (int bit = 31; bit >= 0; --bit)
        result[31 - bit] = ((word >> bit) & 1u) ? '1' : '0';
    return result;
}


std::string trimCopy(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> splitOperands(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) result.push_back(trimCopy(token));
    if (result.size() == 1 && result[0].empty()) result.clear();
    return result;
}

bool isNumber(const std::string& value) {
    if (value.empty()) return false;
    std::size_t i = value[0] == '-' ? 1u : 0u;
    if (i == value.size()) return false;
    for (; i < value.size(); ++i)
        if (value[i] < '0' || value[i] > '9') return false;
    return true;
}

uint32_t registerBits(const std::string& name) {
    return static_cast<uint32_t>(std::stoul(REG_MAP.at(name), nullptr, 2));
}

uint32_t immediateField(const std::string& text) {
    const int value = std::stoi(text);
    return static_cast<uint32_t>(static_cast<uint16_t>(value)) << 1;
}

// Small test-only encoder matching BinaryGenerator's documented field writes.
// It verifies that canonical disassembler output preserves the original words.
std::vector<uint32_t> reencodeCanonicalAssembly(const std::string& assembly) {
    std::vector<std::string> instructions;
    std::unordered_map<std::string, uint32_t> symbols;
    std::istringstream input(assembly);
    std::string line;
    uint32_t address = 0;

    while (std::getline(input, line)) {
        const std::size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line.erase(comment);
        line = trimCopy(line);
        if (line.empty()) continue;
        if (line.back() == ':') {
            symbols[line.substr(0, line.size() - 1)] = address;
            continue;
        }
        instructions.push_back(line);
        address += 4;
    }

    std::vector<uint32_t> result;
    address = 0;
    for (const std::string& instruction : instructions) {
        const std::size_t space = instruction.find_first_of(" \t");
        const std::string mnemonic = space == std::string::npos
            ? instruction : instruction.substr(0, space);
        const std::string operandsText = space == std::string::npos
            ? std::string{} : instruction.substr(space + 1);
        const std::vector<std::string> ops = splitOperands(operandsText);

        if (mnemonic == ".word") {
            const std::string token = ops.at(0);
            result.push_back(static_cast<uint32_t>(std::stoul(token, nullptr, 0)));
            address += 4;
            continue;
        }

        uint32_t word = opcode(mnemonic) << 26;
        const auto isBranch = mnemonic == "b" || mnemonic == "beq" || mnemonic == "bne" ||
                              mnemonic == "bgt" || mnemonic == "blt" || mnemonic == "call";
        if (mnemonic == "nop" || mnemonic == "ret" || mnemonic == "ecall" ||
            mnemonic == "eret" || mnemonic == "ebreak" || mnemonic == "wfi") {
            // SYS_TYPE: remaining 26 bits are zero.
        } else if (mnemonic == "clrz") {
            word |= registerBits(ops.at(0)) << 21;
        } else if (mnemonic == "chk" || mnemonic == "fcmp") {
            word |= registerBits(ops.at(0)) << 17;
            word |= registerBits(ops.at(1)) << 13;
        } else if (mnemonic == "fadd" || mnemonic == "fsub" ||
                   mnemonic == "fmul" || mnemonic == "fdiv") {
            word |= registerBits(ops.at(0)) << 21;
            word |= registerBits(ops.at(1)) << 17;
            word |= registerBits(ops.at(2)) << 13;
        } else if (mnemonic == "ld" || mnemonic == "st") {
            word |= 1u << 25;
            word |= registerBits(ops.at(0)) << 21;
            word |= registerBits(ops.at(1)) << 17;
            word |= immediateField(ops.at(2));
        } else if (mnemonic == "movu" || mnemonic == "movh" || mnemonic == "csrr") {
            word |= 1u << 25;
            word |= registerBits(ops.at(0)) << 21;
            word |= immediateField(ops.at(1));
        } else if (mnemonic == "csrw") {
            word |= 1u << 25;
            word |= registerBits(ops.at(1)) << 17;
            word |= immediateField(ops.at(0));
        } else if (isBranch) {
            const bool conditional = mnemonic == "beq" || mnemonic == "bne" ||
                                     mnemonic == "bgt" || mnemonic == "blt";
            const std::string& label = conditional ? ops.at(2) : ops.at(0);
            const int32_t displacement = static_cast<int32_t>(symbols.at(label)) -
                                         static_cast<int32_t>(address);
            word |= 1u << 25;
            if (conditional) word |= registerBits(ops.at(0)) << 17;
            word |= static_cast<uint32_t>(static_cast<uint16_t>(displacement)) << 1;
        } else {
            // Integer ALU is selected dynamically by operand count / final token.
            if (ops.size() == 3 && !isNumber(ops.at(2))) {
                word |= registerBits(ops.at(0)) << 21;
                word |= registerBits(ops.at(1)) << 17;
                word |= registerBits(ops.at(2)) << 13;
            } else if (ops.size() == 3) {
                word |= 1u << 25;
                word |= registerBits(ops.at(0)) << 21;
                word |= registerBits(ops.at(1)) << 17;
                word |= immediateField(ops.at(2));
            } else if (ops.size() == 2 && !isNumber(ops.at(1))) {
                word |= registerBits(ops.at(0)) << 17;
                word |= registerBits(ops.at(1)) << 13;
            } else if (ops.size() == 2) {
                word |= 1u << 25;
                word |= registerBits(ops.at(0)) << 21;
                word |= immediateField(ops.at(1));
            } else {
                std::abort();
            }
        }
        result.push_back(word);
        address += 4;
    }
    return result;
}

std::string run(const std::vector<uint32_t>& words,
                const risc201::DisassemblerOptions& options = {}) {
    std::ostringstream input;
    for (uint32_t word : words) input << bits(word) << '\n';
    std::istringstream source(input.str());
    std::ostringstream output;
    std::string error;
    const risc201::Disassembler disassembler;
    require(disassembler.disassemble(source, output, options, error), "disassemble succeeds: " + error);
    return output.str();
}

} // namespace

int main() {
    using namespace risc201;

    // Register-register ALU, including the assembler's canonical interpretation
    // of two-operand encodings with a zeroed rd field.
    std::string output = run({r3("add", 3, 1, 2), r2("cmp", 1, 2), r3("mul", 0, 4, 5)});
    require(output.find("add r3, r1, r2") != std::string::npos, "R3 ALU registers decode");
    require(output.find("cmp r0, r1, r2") != std::string::npos, "R2 compare decodes canonically");
    require(output.find("mul r0, r4, r5") != std::string::npos, "zero-rd R-format remains representable");

    // Signed immediate is extracted from [16:1], not sign-extended from raw [16:0].
    output = run({i3("sub", 4, 3, -5)});
    require(output.find("sub r4, r3, -5") != std::string::npos, "negative I3 immediate decodes");

    // Unsigned immediates, CSR operand order, and memory store field order.
    output = run({i2("movu", 8, 50000), i2("movh", 9, 65535), i2("csrr", 1, 255),
                  (opcode("csrw") << 26) | (1u << 25) | (2u << 17) | (255u << 1),
                  i3("st", 5, 14, 12), i3("ld", 6, 14, -4)});
    require(output.find("movu r8, 50000") != std::string::npos, "MOVU unsigned immediate");
    require(output.find("movh r9, 65535") != std::string::npos, "MOVH unsigned immediate");
    require(output.find("csrr r1, 255") != std::string::npos, "CSRR format");
    require(output.find("csrw 255, r2") != std::string::npos, "CSRW operand order");
    require(output.find("st r5, r14, 12") != std::string::npos, "ST data register uses rd field");
    require(output.find("ld r6, r14, -4") != std::string::npos, "LD signed offset");

    // FPU, check, clear, and system instructions.
    DisassemblerOptions aliases;
    aliases.use_register_aliases = true;
    output = run({r3("fadd", 10, 8, 9), r2("fcmp", 8, 11), r2("chk", 1, 2),
                  (opcode("clrz") << 26) | (1u << 21),
                  opcode("nop") << 26, opcode("ret") << 26,
                  opcode("ecall") << 26, opcode("eret") << 26,
                  opcode("ebreak") << 26, opcode("wfi") << 26}, aliases);
    require(output.find("fadd f2, f0, f1") != std::string::npos, "FPU aliases");
    require(output.find("fcmp f0, f3") != std::string::npos, "FCMP");
    require(output.find("chk r1, r2") != std::string::npos, "CHK");
    require(output.find("clrz r1") != std::string::npos, "CLRZ");
    require(output.find("\nnop\n") != std::string::npos, "NOP");
    require(output.find("\nret\n") != std::string::npos, "RET");
    require(output.find("\necall\n") != std::string::npos, "ECALL");
    require(output.find("\neret\n") != std::string::npos, "ERET");
    require(output.find("\nebreak\n") != std::string::npos, "EBREAK");
    require(output.find("\nwfi\n") != std::string::npos, "WFI");

    // Branch labels use the exact byte displacement written by BinaryGenerator.
    output = run({branch("b", 8), branch("beq", 4, 3), opcode("nop") << 26});
    require(output.find("b L_00000008") != std::string::npos, "unconditional branch label");
    require(output.find("beq r3, r12, L_00000008") != std::string::npos,
            "conditional branch's dropped second operand gets canonical filler");
    require(output.find("L_00000008:\n") != std::string::npos, "label is placed at its target");

    // Address annotations are trailing comments, so the instruction remains
    // active when the generated .s file is passed back to the assembler.
    {
        DisassemblerOptions annotated;
        annotated.show_addresses = true;
        annotated.show_machine_words = true;
        output = run({i3("add", 1, 0, 10)}, annotated);
        require(output.find("add r1, r0, 10 ; address=0x00000000 ; word=") != std::string::npos,
                "address and word annotations follow the assembly instruction");
    }

    // With labels disabled, display an absolute 32-bit target without making
    // a false claim that the in-image target is unresolved.
    {
        DisassemblerOptions noLabels;
        noLabels.generate_labels = false;
        output = run({branch("b", 4), opcode("nop") << 26}, noLabels);
        require(output.find("b 0x00000004 ; absolute target address (labels disabled)") !=
                    std::string::npos,
                "no-label mode prints an absolute target with an accurate note");
        require(output.find("L_00000004:") == std::string::npos,
                "no-label mode suppresses generated labels");
    }

    // A negative branch target outside the file is represented as a wrapped
    // 32-bit absolute address, matching the processor's 32-bit PC width.
    output = run({branch("b", -4)});
    require(output.find("b 0xFFFFFFFC ; unresolved target outside this image or unaligned") !=
                std::string::npos,
            "out-of-image negative target uses a wrapped 32-bit address");

    // Negative branches and the terminal label are handled.
    output = run({opcode("nop") << 26, branch("call", -4)});
    require(output.find("call L_00000000") != std::string::npos, "backward call target");
    output = run({branch("b", 4)});
    require(output.find("L_00000004:\n") != std::string::npos, "one-past-end label");

    // Full-format coverage: every instruction class that BinaryGenerator emits.
    std::vector<uint32_t> formatCoverage;
    for (const std::string name : {"add", "sub", "mul", "div", "mod", "cmp",
                                    "and", "or", "xor", "not", "lsl", "lsr", "asr"}) {
        formatCoverage.push_back(r3(name, 7, 3, 12));
        formatCoverage.push_back(r2(name, 3, 12));
        formatCoverage.push_back(i3(name, 7, 3, -32768));
        formatCoverage.push_back(i2(name, 7, 65535));
    }
    for (const std::string name : {"fadd", "fsub", "fmul", "fdiv"})
        formatCoverage.push_back(r3(name, 11, 8, 10));
    formatCoverage.push_back(r2("fcmp", 8, 11));
    formatCoverage.push_back(i3("ld", 15, 14, -32768));
    formatCoverage.push_back(i3("st", 0, 14, 32767));
    formatCoverage.push_back(i2("movu", 8, 65535));
    formatCoverage.push_back(i2("movh", 9, 32768));
    formatCoverage.push_back(i2("csrr", 10, 4095));
    formatCoverage.push_back((opcode("csrw") << 26) | (1u << 25) |
                             (15u << 17) | (4095u << 1));
    formatCoverage.push_back(r2("chk", 13, 14));
    formatCoverage.push_back((opcode("clrz") << 26) | (15u << 21));
    for (const std::string name : {"b", "beq", "bne", "bgt", "blt", "call"}) {
        const unsigned conditionRegister = (name == "b" || name == "call") ? 0u : 5u;
        formatCoverage.push_back(branch(name, 4, conditionRegister));
    }
    for (const std::string name : {"nop", "ret", "ecall", "eret", "ebreak", "wfi"})
        formatCoverage.push_back(opcode(name) << 26);

    output = run(formatCoverage);
    const std::vector<uint32_t> roundTrip = reencodeCanonicalAssembly(output);
    require(roundTrip == formatCoverage,
            "canonical disassembly re-encodes byte-for-byte across every emitted format");

    // The fmov opcode is declared in the map but has no implemented BinaryGenerator
    // format; preserve it as a raw word rather than inventing operands.
    output = run({opcode("fmov") << 26});
    require(output.find(".word 0x60000000") != std::string::npos,
            "unsupported direct fmov encoding is preserved");
    require(reencodeCanonicalAssembly(output) == std::vector<uint32_t>{opcode("fmov") << 26},
            "raw-word fallback preserves the unsupported fmov word");

    // Unknown/reserved encodings are retained as raw words rather than guessed.
    output = run({7u << 26, (opcode("nop") << 26) | 1u});
    require(output.find(".word 0x1C000000 ; unknown opcode") != std::string::npos,
            "unknown opcode fallback");
    require(output.find(".word 0x00000001 ; invalid system-instruction") != std::string::npos,
            "reserved-bit validation");
    require(reencodeCanonicalAssembly(output) ==
                std::vector<uint32_t>{7u << 26, (opcode("nop") << 26) | 1u},
            "unknown and reserved words are preserved by raw-word fallback");

    uint32_t parsed = 0;
    std::string parseError;
    require(Disassembler::parseWordToken("0x1234ABCD", parsed, parseError) &&
            parsed == 0x1234ABCDu, "prefixed hex word parser");
    require(Disassembler::parseWordToken("1234ABCD", parsed, parseError) &&
            parsed == 0x1234ABCDu, "unprefixed hex word parser");
    require(Disassembler::parseWordToken("00000000000000000000000000000001", parsed, parseError) &&
            parsed == 1u, "binary word parser");
    require(!Disassembler::parseWordToken("not-a-word", parsed, parseError),
            "malformed token rejected");
    require(Disassembler::parseWordToken("00000000000000000000000000000000", parsed, parseError) &&
            parseError.empty(), "successful token parse clears prior error text");

    // Text parser supports one word per line, comments, blank lines, and hex tokens.
    {
        std::istringstream input("; header\n0x00000000 # nop\n\n" +
                                 bits(opcode("ebreak") << 26) + "\n");
        std::ostringstream result;
        std::string error;
        Disassembler disassembler;
        require(disassembler.disassemble(input, result, {}, error), "mixed text input parse");
        require(result.str().find("nop") != std::string::npos, "hex input decodes");
        require(result.str().find("ebreak") != std::string::npos, "binary input decodes");
    }

    // File API leaves an existing destination intact when parsing fails.
    {
        const std::string inputPath = "build/disassembler/malformed_input.bin";
        const std::string outputPath = "build/disassembler/preserved_output.s";
        {
            std::ofstream badInput(inputPath, std::ios::binary);
            badInput << "this-is-not-a-word\n";
        }
        {
            std::ofstream existingOutput(outputPath, std::ios::binary);
            existingOutput << "keep-this-output\n";
        }
        Disassembler disassembler;
        std::string error;
        require(!disassembler.disassembleFile(inputPath, outputPath, {}, error),
                "file API rejects malformed input");
        std::ifstream preserved(outputPath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(preserved)),
                             std::istreambuf_iterator<char>());
        require(contents == "keep-this-output\n",
                "failed disassembly does not destroy an existing destination");
        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    // In-place disassembly is safe because the input is fully parsed/rendered
    // before the destination is opened and truncated.
    {
        const std::string path = "build/disassembler/in_place.bin";
        {
            std::ofstream file(path, std::ios::binary);
            file << bits(opcode("ebreak") << 26) << '\n';
        }
        Disassembler disassembler;
        std::string error;
        require(disassembler.disassembleFile(path, path, {}, error),
                "in-place file disassembly succeeds");
        std::ifstream file(path, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        require(contents.find("ebreak") != std::string::npos,
                "in-place file disassembly retains decoded output");
        std::remove(path.c_str());
    }

    // Raw little-endian word input.
    {
        const uint32_t rawWord = opcode("ebreak") << 26;
        std::string raw(4, '\0');
        raw[0] = static_cast<char>(rawWord & 0xFFu);
        raw[1] = static_cast<char>((rawWord >> 8) & 0xFFu);
        raw[2] = static_cast<char>((rawWord >> 16) & 0xFFu);
        raw[3] = static_cast<char>((rawWord >> 24) & 0xFFu);
        std::istringstream input(raw);
        std::ostringstream result;
        std::string error;
        DisassemblerOptions rawOptions;
        rawOptions.input_format = InputFormat::RawLittleEndian;
        Disassembler disassembler;
        require(disassembler.disassemble(input, result, rawOptions, error), "raw little-endian input parse");
        require(result.str().find("ebreak") != std::string::npos, "raw little-endian word decodes");
    }
    {
        std::istringstream input(std::string("\x01\x02\x03", 3));
        std::ostringstream result;
        std::string error;
        DisassemblerOptions rawOptions;
        rawOptions.input_format = InputFormat::RawLittleEndian;
        Disassembler disassembler;
        require(!disassembler.disassemble(input, result, rawOptions, error),
                "truncated raw input is rejected");
    }

    std::cout << "All RISC201 disassembler tests passed.\n";
    return 0;
}
