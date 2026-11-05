#ifndef TEST_SUPPORT_HPP
#define TEST_SUPPORT_HPP

// ---------------------------------------------------------------------------
// Minimal unit-test support: a test registry, CHECK / CHECK_EQ macros, and
// helpers that encode RISC201 instruction words.
//
// Instruction encoding used by the helpers (must match instruction_decode.cpp):
//   [31:25] opcode(6) + format bit    [24:21] rd    [20:17] rs1
//   [16:13] rs2 (R-format)            [16:0]  17-bit immediate (I-format)
//   [24:0]  25-bit branch / call offset
// Opcodes come straight from OPCODE_MAP, so the tests follow the ISA config.
// ---------------------------------------------------------------------------

#include <bitset>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "isa_config.hpp"

namespace tf {

using TestFn = void (*)();

inline std::vector<std::pair<std::string, TestFn>>& registry() {
    static std::vector<std::pair<std::string, TestFn>> r;
    return r;
}
inline int& checkCount() { static int n = 0; return n; }
inline int& failCount()  { static int n = 0; return n; }

struct Registrar {
    Registrar(const char* name, TestFn fn) { registry().emplace_back(name, fn); }
};

inline void check(bool ok, const char* expr, const char* file, int line) {
    ++checkCount();
    if (!ok) {
        ++failCount();
        std::cout << "    FAIL " << file << ":" << line << "  " << expr << "\n";
    }
}

inline void checkEq(unsigned long long got, unsigned long long want,
                    const char* eGot, const char* eWant,
                    const char* file, int line) {
    ++checkCount();
    if (got != want) {
        ++failCount();
        std::cout << "    FAIL " << file << ":" << line << "  " << eGot << " == " << eWant
                  << "   (got 0x" << std::hex << got << ", expected 0x" << want
                  << std::dec << ")\n";
    }
}

inline int runAll() {
    int failedTests = 0;
    for (auto& t : registry()) {
        const int before = failCount();
        try {
            t.second();
        } catch (const std::exception& e) {
            ++failCount();
            std::cout << "    EXCEPTION: " << e.what() << "\n";
        } catch (...) {
            ++failCount();
            std::cout << "    UNKNOWN EXCEPTION\n";
        }
        const bool ok = (failCount() == before);
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.first << "\n";
    }
    std::cout << "\n" << registry().size() << " tests, " << checkCount()
              << " checks, " << failCount() << " failed checks\n";
    return failedTests == 0 ? 0 : 1;
}

// ---- instruction encoders --------------------------------------------------

inline std::string toBits(uint32_t word) {
    return std::bitset<32>(word).to_string();
}

inline uint32_t opcode7(const std::string& mnemonic, bool immFormat) {
    const std::string key = OPCODE_MAP.at(mnemonic) + (immFormat ? '1' : '0');
    return static_cast<uint32_t>(std::bitset<7>(key).to_ulong());
}

inline uint32_t encR(const std::string& mn, uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return (opcode7(mn, false) << 25) | ((rd & 0xFu) << 21) |
           ((rs1 & 0xFu) << 17) | ((rs2 & 0xFu) << 13);
}

inline uint32_t encI(const std::string& mn, uint32_t rd, uint32_t rs1, uint32_t imm) {
    return (opcode7(mn, true) << 25) | ((rd & 0xFu) << 21) |
           ((rs1 & 0xFu) << 17) | (imm & 0x1FFFFu);
}

inline uint32_t encBranch(const std::string& mn, uint32_t offset) {
    return (opcode7(mn, true) << 25) | (offset & 0x1FFFFFFu);
}

} // namespace tf

#define CHECK(cond) ::tf::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::tf::checkEq(static_cast<unsigned long long>(a), \
                                     static_cast<unsigned long long>(b), \
                                     #a, #b, __FILE__, __LINE__)
#define TEST_CASE(name) \
    static void name(); \
    static ::tf::Registrar registrar_##name(#name, &name); \
    static void name()

#endif