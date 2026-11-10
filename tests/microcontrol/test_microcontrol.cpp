// Tests for the standalone microcontrol model.
//
// Build/run with:
//   make -f Makefile.microcontrol test

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "control_signals.hpp"
#include "decode_micro_unit.hpp"
#include "execute_micro_unit.hpp"
#include "isa_config.hpp"
#include "microcontrol_unit.hpp"
#include "microprogrammed_cpu.hpp"
#include "microprogram_store.hpp"
#include "microsequencer.hpp"
#include "shared_bus.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) {
        std::cout << "[ PASS ] " << what << "\n";
    } else {
        std::cout << "[ FAIL ] " << what << "\n";
        ++failures;
    }
}

void checkEq(std::uint32_t got, std::uint32_t want, const std::string& what) {
    if (got == want) {
        std::cout << "[ PASS ] " << what << " = " << got << "\n";
    } else {
        std::cout << "[ FAIL ] " << what << " = " << got
                  << " (want " << want << ")\n";
        ++failures;
    }
}

std::uint32_t opcodeOf(const std::string& mnemonic) {
    const std::string& bits = OPCODE_MAP.at(mnemonic);
    std::uint32_t op = 0;
    for (char c : bits)
        op = (op << 1) | (c == '1' ? 1u : 0u);
    return op;
}

std::uint32_t encR(const std::string& mn, int rd, int rs1, int rs2) {
    return (opcodeOf(mn) << 26) | (static_cast<std::uint32_t>(rd) << 21) |
           (static_cast<std::uint32_t>(rs1) << 17) |
           (static_cast<std::uint32_t>(rs2) << 13);
}

std::uint32_t encI(const std::string& mn, int rd, int rs1, std::uint32_t imm) {
    return (opcodeOf(mn) << 26) | (1u << 25) |
           (static_cast<std::uint32_t>(rd) << 21) |
           (static_cast<std::uint32_t>(rs1) << 17) |
           ((imm & 0xFFFFu) << 1);
}

std::uint32_t encBranch(const std::string& mn, std::uint32_t byte_offset) {
    return (opcodeOf(mn) << 26) | (1u << 25) | (byte_offset & 0x1FFFFFFu);
}

std::uint32_t encFmov(int rd, std::uint32_t imm21) {
    return (opcodeOf("fmov") << 26) | (static_cast<std::uint32_t>(rd) << 21) |
           (imm21 & 0x1FFFFFu);
}

std::uint32_t encSys(const std::string& mn) {
    return opcodeOf(mn) << 26;
}

std::string toBits(std::uint32_t v) {
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i)
        s[static_cast<std::size_t>(31 - i)] = ((v >> i) & 1u) ? '1' : '0';
    return s;
}

std::vector<std::string> bits(const std::vector<std::uint32_t>& words) {
    std::vector<std::string> out;
    out.reserve(words.size());
    for (auto w : words) out.push_back(toBits(w));
    return out;
}

void testBusAndSequencer() {
    mc::SharedBus bus;
    bus.drive(mc::BusDriver::Pc, 7);
    bus.drive(mc::BusDriver::RegA, 9);
    check(bus.conflict(), "shared bus reports two different drivers");
    checkEq(bus.read(), 9, "shared bus holds the last value");

    mc::Microsequencer seq;
    seq.begin(3);
    check(seq.valid(), "sequencer starts valid");
    seq.next();
    seq.next();
    seq.next();
    check(seq.done(), "sequencer reaches Done after three micro-ops");

    seq.begin(1);
    check(seq.valid(), "single micro-op starts valid");
    seq.next();
    check(seq.done(), "single micro-op finishes");

    seq.begin(1);
    check(seq.repeat(2), "repeat 1 of 2 stays on the micro-op");
    check(seq.repeat(2), "repeat 2 of 2 stays on the micro-op");
    check(!seq.repeat(2), "third repeat request advances");
}

void testStoreKeys() {
    mc::MicroprogramStore store;

    const std::uint16_t addR = store.keyOf("add", false);
    const std::uint16_t addI = store.keyOf("add", true);
    const std::uint16_t ldI  = store.keyOf("ld", true);
    const std::uint16_t mulR = store.keyOf("mul", false);

    check(store.find(mc::Stage::Decode, addR) != nullptr, "add R has an ID microprogram");
    check(store.find(mc::Stage::Execute, addR) != nullptr, "add R has an EX microprogram");
    check(store.find(mc::Stage::Writeback, addR) != nullptr, "add R has a WB microprogram");
    check(store.find(mc::Stage::Decode, addI) != nullptr, "add I has an ID microprogram");
    check(store.find(mc::Stage::Execute, ldI) != nullptr, "ld has an EX microprogram");
    check(store.find(mc::Stage::Ma1, ldI) != nullptr, "ld has an MA1 microprogram");
    check(store.find(mc::Stage::Ma2, ldI) != nullptr, "ld has an MA2 microprogram");
    check(store.find(mc::Stage::Writeback, ldI) != nullptr, "ld has a WB microprogram");
    check(store.find(mc::Stage::Execute, mulR) != nullptr, "mul has an EX microprogram");

    const ControlWord addWord = store.legacyWord(addR);
    check(addWord.test(static_cast<std::size_t>(Signal::IS_ADD)), "add projects IS_ADD");
    check(addWord.test(static_cast<std::size_t>(Signal::IS_WB)), "add projects IS_WB");

    const ControlWord ldWord = store.legacyWord(ldI);
    check(ldWord.test(static_cast<std::size_t>(Signal::IS_LD)), "ld projects IS_LD");
    check(ldWord.test(static_cast<std::size_t>(Signal::IS_WB)), "ld projects IS_WB");
    check(ldWord.test(static_cast<std::size_t>(Signal::IS_ADD)), "ld projects IS_ADD");

    const ControlWord beqWord = store.legacyWord(store.keyOf("beq", true));
    check(beqWord.test(static_cast<std::size_t>(Signal::IS_BEQ)), "beq projects IS_BEQ");
    check(beqWord.test(static_cast<std::size_t>(Signal::IS_IMMEDIATE)),
          "beq projects IS_IMMEDIATE");

    // Listing support for the integrated simulator CLI.
    const std::vector<std::uint16_t> keys = store.keys();
    check(!keys.empty(), "store exposes its instruction keys");
    check(std::find(keys.begin(), keys.end(), addR) != keys.end(),
          "key listing contains add R");
    check(keys.size() == store.keyCount(), "key listing covers every legacy key");
}

void testControlRomEntries() {
    MicroprogrammedControlUnit rom;
    const auto& entries = rom.entries();
    check(entries.size() == rom.size(), "control ROM exposes every entry");
    check(entries.find(mc::MicroprogramStore::keyString(
              mc::MicroprogramStore::keyOf("add", false))) != entries.end(),
          "control ROM contains the add R key");
}

void testDecodeUnit() {
    mc::MicroprogramStore store;
    RegisterFile rf;
    rf.write(1, 10);
    rf.write(2, 20);
    rf.write(3, 30);

    mc::DecodeMicroControlUnit dec(store, rf);

    mc::LatchIFID in;
    in.pc = 0;
    in.pc_next = 1;
    in.ir = encR("add", 4, 1, 2);
    in.valid = true;

    mc::LatchIDEX out;
    check(dec.clock(in, out, false, false), "ID accepts add R");
    check(out.valid, "ID/EX valid for add R");
    checkEq(out.rd, 4, "add R rd");
    checkEq(out.a, 10, "add R operand A");
    checkEq(out.b, 20, "add R operand B");
    checkEq(out.micro_key, store.keyOf("add", false), "add R micro key");

    const auto info = dec.peek(in);
    check(info.valid && info.reads_a && info.reads_b, "hazard peek sees both sources");
    check(info.writes_rf && info.dst_rf == 4, "hazard peek sees the destination");

    mc::LatchIFID ld;
    ld.pc = 1;
    ld.pc_next = 2;
    ld.ir = encI("ld", 5, 3, 8);
    ld.valid = true;
    check(dec.clock(ld, out, false, false), "ID accepts ld");
    checkEq(out.rd, 5, "ld rd");
    checkEq(out.a, 30, "ld base register");
    checkEq(out.imm, 8, "ld immediate");

    const auto ld_info = dec.peek(ld);
    check(ld_info.reads_a && !ld_info.reads_b, "ld reads only its base register");
    check(ld_info.writes_rf && ld_info.dst_rf == 5, "ld writes its destination");
}

void testExecuteUnit() {
    mc::MicroprogramStore store;
    RegisterFile rf;
    rf.write(1, 6);
    rf.write(2, 7);
    ControlStatusUnit csu;
    mc::DecodeMicroControlUnit dec(store, rf);
    mc::ExecuteMicroControlUnit ex(store, csu);

    mc::LatchIFID in;
    in.pc = 0;
    in.pc_next = 1;
    in.ir = encR("add", 3, 1, 2);
    in.valid = true;

    mc::LatchIDEX id;
    dec.clock(in, id, false, false);

    mc::LatchMem em;
    auto sig = ex.clock(id, em);
    check(em.valid, "EX commits add");
    checkEq(em.result, 13, "add result");
    check(em.is_wb, "add writes back");
    check(!sig.branch_taken, "add is not a branch");

    // mul r4, r1, r2 -> 42, four EX cycles with a bubble on the first three.
    mc::LatchIFID mul_in;
    mul_in.pc = 1;
    mul_in.pc_next = 2;
    mul_in.ir = encR("mul", 4, 1, 2);
    mul_in.valid = true;
    dec.clock(mul_in, id, false, false);

    ex.reset();
    for (int i = 0; i < 3; ++i) {
        auto s = ex.clock(id, em);
        check(s.busy && !em.valid, "mul bubbles while the multiplier runs");
    }
    check(ex.isBusy(), "mul still owns EX on the third bubble");
    auto s = ex.clock(id, em);
    check(!s.busy && em.valid, "mul commits on the fourth EX cycle");
    checkEq(em.result, 42, "mul result");
    checkEq(ex.microPC(), 3, "EX microPC ends on the last micro-op");
    check(ex.lastTrace().size() == 4, "EX trace has four micro-ops");

    // FPU: 1.0f + 2.0f = 3.0f
    rf.write(9, 0x3F800000u);
    rf.write(10, 0x40000000u);
    mc::LatchIFID fadd_in;
    fadd_in.pc = 2;
    fadd_in.pc_next = 3;
    fadd_in.ir = encR("fadd", 8, 9, 10);
    fadd_in.valid = true;
    dec.clock(fadd_in, id, false, false);
    ex.reset();
    ex.clock(id, em);
    checkEq(em.result, 0x40400000u, "fadd 1.0 + 2.0 = 3.0");
}

void runWhole(bool four, const std::vector<std::uint32_t>& words,
              const std::string& label) {
    mc::MicroprogrammedCPU cpu(four);
    cpu.loadProgram(bits(words));
    cpu.run(100000);

    const auto& r = cpu.registers();
    check(cpu.halted(), label + " halts");
    checkEq(r[3], 13, label + " add result");
    checkEq(r[4], 91, label + " mul result");
    checkEq(r[5], 91, label + " load result");
    std::uint32_t mem = 0;
    cpu.readMemory(6, mem);
    checkEq(mem, 91, label + " store result");
    check(cpu.totalCycles() > 0, label + " counts cycles");
}

void testEndToEnd() {
    const std::vector<std::uint32_t> program = {
        encI("movu", 1, 0, 6),
        encI("movu", 2, 0, 7),
        encR("add", 3, 1, 2),
        encR("mul", 4, 3, 2),
        encI("st", 4, 1, 0),
        encI("ld", 5, 1, 0),
    };
    runWhole(false, program, "6-stage");
    runWhole(true, program, "4-stage");
}

void testBranch() {
    for (bool four : {false, true}) {
        mc::MicroprogrammedCPU cpu(four);
        const std::vector<std::uint32_t> program = {
            encI("movu", 1, 0, 5),
            encI("movu", 2, 0, 5),
            encR("cmp", 0, 1, 2),
            encBranch("beq", 8),        // skip the next instruction
            encI("movu", 3, 0, 111),
            encI("movu", 3, 0, 222),
        };
        cpu.loadProgram(bits(program));
        cpu.run(100000);

        const std::string name = four ? "4-stage" : "6-stage";
        check(cpu.halted(), name + " branch program halts");
        checkEq(cpu.registers()[3], 222, name + " taken beq skips the wrong path");
    }
}

void testAluFamily() {
    for (bool four : {false, true}) {
        mc::MicroprogrammedCPU cpu(four);
        const std::vector<std::uint32_t> program = {
            encI("movu", 1, 0, 20),
            encI("movu", 2, 0, 6),
            encR("add", 3, 1, 2),       // 26
            encR("sub", 4, 1, 2),       // 14
            encR("and", 5, 1, 2),       // 4
            encR("or",  6, 1, 2),       // 22
            encR("xor", 7, 1, 2),       // 18
            encR("not", 8, 0, 2),       // ~6
            encR("lsl", 9, 1, 2),       // 20 << 6
            encR("lsr", 10, 1, 2),      // 20 >> 6
            encR("asr", 11, 1, 2),      // 20 >> 6
            encI("add", 0, 1, 5),       // 25
            encR("div", 5, 1, 2),       // 3
            encR("mod", 6, 1, 2),       // 2
        };
        cpu.loadProgram(bits(program));
        cpu.run(100000);

        const std::string name = four ? "4-stage" : "6-stage";
        const auto& r = cpu.registers();
        check(cpu.halted(), name + " ALU program halts");
        checkEq(r[3], 26, name + " add");
        checkEq(r[4], 14, name + " sub");
        checkEq(r[5], 3, name + " div (overwrites and)");
        checkEq(r[6], 2, name + " mod (overwrites or)");
        checkEq(r[7], 18, name + " xor");
        checkEq(r[8], 0xFFFFFFF9u, name + " not");
        checkEq(r[9], 1280, name + " lsl");
        checkEq(r[10], 0, name + " lsr");
        checkEq(r[11], 0, name + " asr");
        checkEq(r[0], 25, name + " add immediate");
    }
}

void testCallRet() {
    for (bool four : {false, true}) {
        mc::MicroprogrammedCPU cpu(four);
        const std::vector<std::uint32_t> program = {
            encBranch("b", 16),         // 0: jump to main at 4
            encI("movu", 9, 0, 99),     // 1: must be skipped
            encI("movu", 3, 0, 7),      // 2: function body
            encSys("ret"),              // 3: return
            encBranch("call", static_cast<std::uint32_t>(-8) & 0x1FFFFFFu), // 4: -> 2
            encI("movu", 2, 0, 1),      // 5: return target
        };
        cpu.loadProgram(bits(program));
        cpu.run(100000);

        const std::string name = four ? "4-stage" : "6-stage";
        const auto& r = cpu.registers();
        check(cpu.halted(), name + " call/ret program halts");
        checkEq(r[2], 1, name + " execution resumes after ret");
        checkEq(r[3], 7, name + " function body ran");
        checkEq(r[9], 0, name + " branch skipped the speculative instruction");
    }
}

void testCsrAndClear() {
    for (bool four : {false, true}) {
        mc::MicroprogrammedCPU cpu(four);
        const std::vector<std::uint32_t> program = {
            encI("movu", 1, 0, 3),
            encI("csrw", 0, 1, 0),      // flags = 3 (E=1, GT=1)
            encI("csrr", 2, 0, 0),      // r2 = flags
            encI("movu", 3, 0, 42),
            encR("clrz", 3, 0, 0),      // r3 = 0
        };
        cpu.loadProgram(bits(program));
        cpu.run(100000);

        const std::string name = four ? "4-stage" : "6-stage";
        const auto& r = cpu.registers();
        check(cpu.halted(), name + " CSR/clrz program halts");
        checkEq(r[2], 3, name + " csrr reads the csrw value");
        checkEq(r[3], 0, name + " clrz clears its destination");
    }
}

void testFpuFamily() {
    for (bool four : {false, true}) {
        mc::MicroprogrammedCPU cpu(four);
        const std::vector<std::uint32_t> program = {
            encI("movh", 9, 0, 0x3F80),   // 1.0f
            encI("movh", 10, 0, 0x4000),  // 2.0f
            encFmov(7, 0x12345),          // fmov immediate
            encR("fadd", 8, 9, 10),       // 3.0f
            encR("fmul", 11, 9, 10),      // 2.0f
            encR("fdiv", 6, 8, 10),       // 1.5f
            encR("fcmp", 0, 9, 10),       // 1.0 < 2.0
            encBranch("blt", 8),          // taken, skip next
            encI("movu", 3, 0, 111),
            encI("movu", 3, 0, 222),
        };
        cpu.loadProgram(bits(program));
        cpu.run(100000);

        const std::string name = four ? "4-stage" : "6-stage";
        const auto& r = cpu.registers();
        check(cpu.halted(), name + " FPU program halts");
        checkEq(r[7], 0x12345u, name + " fmov immediate");
        checkEq(r[8], 0x40400000u, name + " fadd 3.0");
        checkEq(r[11], 0x40000000u, name + " fmul 2.0");
        checkEq(r[6], 0x3FC00000u, name + " fdiv 1.5");
        checkEq(r[3], 222, name + " fcmp + blt path");
    }
}

void testHaltOps() {
    for (bool four : {false, true}) {
        const std::string name = four ? "4-stage" : "6-stage";
        {
            mc::MicroprogrammedCPU cpu(four);
            cpu.loadProgram(bits({
                encI("movu", 1, 0, 1),
                encSys("ebreak"),
                encI("movu", 2, 0, 2),
            }));
            cpu.run(100000);
            check(cpu.halted(), name + " ebreak halts");
            checkEq(cpu.registers()[1], 1, name + " ebreak preserves prior work");
        }
        {
            mc::MicroprogrammedCPU cpu(four);
            cpu.loadProgram(bits({
                encI("movu", 1, 0, 1),
                encSys("wfi"),
                encI("movu", 2, 0, 2),
            }));
            cpu.run(100000);
            check(cpu.halted(), name + " wfi halts");
            checkEq(cpu.registers()[1], 1, name + " wfi preserves prior work");
        }
    }
}

void testMicroprogrammedCpuDebugState() {
    mc::MicroprogrammedCPU cpu(false);
    cpu.loadProgram(bits({
        encI("movu", 1, 0, 1),
        encI("movu", 2, 0, 2),
    }));
    cpu.step();
    cpu.dumpMicro();
    check(cpu.totalCycles() == 1, "CPU counts one clock per step");
}

} // namespace

int main() {
    std::cout << "=== Microcontrol model tests ===\n";
    testBusAndSequencer();
    testStoreKeys();
    testControlRomEntries();
    testDecodeUnit();
    testExecuteUnit();
    testEndToEnd();
    testBranch();
    testAluFamily();
    testCallRet();
    testCsrAndClear();
    testFpuFamily();
    testHaltOps();
    testMicroprogrammedCpuDebugState();

    if (failures == 0) {
        std::cout << "\nAll microcontrol tests passed.\n";
        return 0;
    }
    std::cout << "\n" << failures << " microcontrol test(s) failed.\n";
    return 1;
}
