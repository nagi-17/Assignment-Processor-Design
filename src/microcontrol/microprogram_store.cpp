#include "microprogram_store.hpp"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#include "isa_config.hpp"

namespace mc {
namespace {

enum class ImmKind { None, Signed16, Branch, Movu, Movh, Csr, Fmov };

using MI = Microinstruction;

MI immTransfer(ImmKind kind) {
    switch (kind) {
        case ImmKind::Signed16: return MI::transfer(BusDriver::IrImmSigned16, D_IMM, "IR[imm]->IMM");
        case ImmKind::Branch:   return MI::transfer(BusDriver::IrImmBranch,   D_IMM, "IR[offset]->IMM");
        case ImmKind::Movu:     return MI::transfer(BusDriver::IrImmMovu,     D_IMM, "IR[imm16]->IMM");
        case ImmKind::Movh:     return MI::transfer(BusDriver::IrImmMovh,     D_IMM, "IR[imm16]<<16->IMM");
        case ImmKind::Csr:      return MI::transfer(BusDriver::IrImmCsr,      D_IMM, "IR[csr]->IMM");
        case ImmKind::Fmov:     return MI::transfer(BusDriver::IrImmFmov,     D_IMM, "IR[imm21]->IMM");
        case ImmKind::None:     break;
    }
    return MI::commit();
}

// Generic operand-fetch microprogram.
MicroProgram decProgram(bool need_rs1, bool need_rs2, bool use_a, bool use_b,
                        ImmKind imm) {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRd, D_RD, "IR[rd]->RD"));
    if (need_rs1) p.push_back(MI::transfer(BusDriver::IrRs1, D_RS1, "IR[rs1]->RS1"));
    if (need_rs2) p.push_back(MI::transfer(BusDriver::IrRs2, D_RS2, "IR[rs2]->RS2"));
    if (use_a)    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[rs1]->A"));
    if (use_b)    p.push_back(MI::transfer(BusDriver::RfB, D_B, "RF[rs2]->B"));
    if (imm != ImmKind::None) p.push_back(immTransfer(imm));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decStoreProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRd, D_RD | D_RS2, "IR[rd]->RD,RS2"));
    p.push_back(MI::transfer(BusDriver::IrRs1, D_RS1, "IR[rs1]->RS1"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[rs1]->A"));
    p.push_back(MI::transfer(BusDriver::RfB, D_B, "RF[rd]->B"));
    p.push_back(MI::transfer(BusDriver::IrImmSigned16, D_IMM, "IR[imm]->IMM"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decLoadProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRd, D_RD, "IR[rd]->RD"));
    p.push_back(MI::transfer(BusDriver::IrRs1, D_RS1, "IR[rs1]->RS1"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[rs1]->A"));
    p.push_back(MI::transfer(BusDriver::IrImmSigned16, D_IMM, "IR[imm]->IMM"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decSimpleProgram(ImmKind imm) {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRd, D_RD, "IR[rd]->RD"));
    if (imm != ImmKind::None) p.push_back(immTransfer(imm));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decRetProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::ConstRA, D_RS1, "ra->RS1"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[ra]->A"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decSyscallProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::Const1, D_RS1, "const 1->RS1"));
    p.push_back(MI::transfer(BusDriver::Const2, D_RS2, "const 2->RS2"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[r1]->A"));
    p.push_back(MI::transfer(BusDriver::RfB, D_B, "RF[r2]->B"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decCsrwProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRs1, D_RS1, "IR[rs1]->RS1"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[rs1]->A"));
    p.push_back(MI::transfer(BusDriver::IrImmCsr, D_IMM, "IR[csr]->IMM"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decChkProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::IrRs1, D_RS1, "IR[rs1]->RS1"));
    p.push_back(MI::transfer(BusDriver::IrRs2, D_RS2, "IR[rs2]->RS2"));
    p.push_back(MI::transfer(BusDriver::RfA, D_A, "RF[rs1]->A"));
    p.push_back(MI::transfer(BusDriver::RfB, D_B, "RF[rs2]->B"));
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram decCommitOnly() {
    MicroProgram p;
    p.push_back(MI::commit("commit ID/EX"));
    return p;
}

MicroProgram exAluProgram(MicroAluOp op, bool use_imm, bool unary_b) {
    MicroProgram p;
    if (unary_b) {
        // The ALU's NOT inverts operand A, so place the single source there.
        p.push_back(MI::transfer(use_imm ? BusDriver::Imm : BusDriver::RegB,
                                 D_ALU_A, use_imm ? "IMM->ALU_A" : "B->ALU_A"));
        p.push_back(MI::transfer(BusDriver::Zero, D_ALU_B, "0->ALU_B"));
    } else {
        p.push_back(MI::transfer(BusDriver::RegA, D_ALU_A, "A->ALU_A"));
        p.push_back(MI::transfer(use_imm ? BusDriver::Imm : BusDriver::RegB,
                                 D_ALU_B, use_imm ? "IMM->ALU_B" : "B->ALU_B"));
    }
    p.push_back(MI::alu(BusDriver::AluOut, D_RESULT, op, "ALUOut->RESULT"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exFpuProgram(MicroFpuOp op) {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_FPU_A, "A->FPU_A"));
    p.push_back(MI::transfer(BusDriver::RegB, D_FPU_B, "B->FPU_B"));
    p.push_back(MI::fpu(BusDriver::FpuOut, D_RESULT, op, "FPUOut->RESULT"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exLoadProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_ALU_A, "A->ALU_A"));
    p.push_back(MI::transfer(BusDriver::Imm, D_ALU_B, "IMM->ALU_B"));
    p.push_back(MI::alu(BusDriver::AluOut, D_RESULT | D_MAR, MicroAluOp::Add,
                        "AGU->RESULT,MAR"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exStoreProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_ALU_A, "A->ALU_A"));
    p.push_back(MI::transfer(BusDriver::Imm, D_ALU_B, "IMM->ALU_B"));
    p.push_back(MI::transfer(BusDriver::RegB, D_ST_DATA, "B->ST_DATA"));
    p.push_back(MI::alu(BusDriver::AluOut, D_RESULT | D_MAR, MicroAluOp::Add,
                        "AGU->RESULT,MAR"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exBranchProgram(BranchCond cond, bool link) {
    MicroProgram p;
    if (link)
        p.push_back(MI::transfer(BusDriver::PcNext, D_RESULT, "PC+1->RESULT"));
    p.push_back(MI::transfer(BusDriver::PcPlusImm, D_BRANCH_TARGET, "PC+offset->TARGET"));
    p.push_back(MI::branchOp(cond, D_NONE, "evaluate branch"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exRetProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_BRANCH_TARGET, "ra->TARGET"));
    p.push_back(MI::branchOp(BranchCond::Always, D_NONE, "always taken"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exCsrrProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::CsrOut, D_RESULT, "CSR->RESULT"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exCsrwProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_CSR_IN, "A->CSR_IN"));
    p.push_back(MI::effectOp(MicroEffect::CsrWrite, "write CSR"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exImmProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::Imm, D_RESULT, "IMM->RESULT"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exClearProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::Zero, D_RESULT, "0->RESULT"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exChkProgram() {
    MicroProgram p;
    p.push_back(MI::transfer(BusDriver::RegA, D_ALU_A, "A->ALU_A"));
    p.push_back(MI::transfer(BusDriver::RegB, D_ALU_B, "B->ALU_B"));
    p.push_back(MI::alu(BusDriver::AluOut, D_NONE, MicroAluOp::Cmp, "compare bounds"));
    p.push_back(MI::effectOp(MicroEffect::Chk, "raise stack fault if A>=B"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exTrapProgram() {
    MicroProgram p;
    p.push_back(MI::effectOp(MicroEffect::Trap, "system trap"));
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram exCommitOnly() {
    MicroProgram p;
    p.push_back(MI::commit("commit EX/MEM"));
    return p;
}

MicroProgram ma1Program(bool mem) {
    MicroProgram p;
    if (mem) {
        p.push_back(MI::transfer(BusDriver::ExResult, D_MAR, "EXResult->MAR"));
        p.push_back(MI::mem(MicroMemOp::Probe, D_NONE, "tag probe"));
    }
    p.push_back(MI::commit("commit MA1/MA2"));
    return p;
}

MicroProgram ma2Program(bool load, bool store) {
    MicroProgram p;
    if (load) {
        p.push_back(MI::mem(MicroMemOp::Read, D_NONE, "memory read"));
        p.push_back(MI::transfer(BusDriver::Mdr, D_READ_DATA, "MDR->READ_DATA"));
    } else if (store) {
        p.push_back(MI::mem(MicroMemOp::Write, D_NONE, "memory write"));
    }
    p.push_back(MI::commit("commit MA2/WB"));
    return p;
}

MicroProgram wbProgram(bool load, bool wb) {
    MicroProgram p;
    if (wb)
        p.push_back(MI::transfer(load ? BusDriver::ReadData : BusDriver::ExResult,
                                 D_RF_WRITE, "write data->RF"));
    p.push_back(MI::commit("commit WB"));
    return p;
}

MicroProgram mergedProgram(bool load, bool store, bool wb) {
    MicroProgram p;
    if (load || store) {
        p.push_back(MI::transfer(BusDriver::ExResult, D_MAR, "EXResult->MAR"));
        p.push_back(MI::mem(MicroMemOp::Probe, D_NONE, "tag probe"));
    }
    if (load) {
        p.push_back(MI::mem(MicroMemOp::Read, D_NONE, "memory read"));
        p.push_back(MI::transfer(BusDriver::Mdr, D_READ_DATA, "MDR->READ_DATA"));
    } else if (store) {
        p.push_back(MI::mem(MicroMemOp::Write, D_NONE, "memory write"));
    }
    if (wb)
        p.push_back(MI::transfer(load ? BusDriver::ReadData : BusDriver::ExResult,
                                 D_RF_WRITE, "write data->RF"));
    p.push_back(MI::commit("commit MEM/WB"));
    return p;
}

} // namespace

// ---------------------------------------------------------------------------

uint16_t MicroprogramStore::keyOf(const std::string& mnemonic, bool imm_format) {
    const std::string& opc = OPCODE_MAP.at(mnemonic);
    if (opc.size() != 6)
        throw std::logic_error("Opcode of '" + mnemonic + "' is not 6 bits");
    uint16_t op = 0;
    for (char c : opc) {
        if (c != '0' && c != '1')
            throw std::logic_error("Non-binary opcode for '" + mnemonic + "'");
        op = static_cast<uint16_t>((op << 1) | (c == '1' ? 1u : 0u));
    }
    return static_cast<uint16_t>((op << 1) | (imm_format ? 1u : 0u));
}

std::string MicroprogramStore::keyString(uint16_t key) {
    std::string s;
    for (int bit = 6; bit >= 0; --bit)
        s.push_back(((key >> bit) & 1u) ? '1' : '0');
    return s;
}

void MicroprogramStore::add(Stage stage, uint16_t key, MicroProgram program) {
    programs_[composite(stage, key)] = std::move(program);
}

void MicroprogramStore::addLegacy(uint16_t key, std::initializer_list<Signal> signals) {
    ControlWord& word = legacy_[key];
    for (Signal s : signals)
        word.set(static_cast<std::size_t>(s));
}

const MicroProgram* MicroprogramStore::find(Stage stage, uint16_t key) const {
    auto it = programs_.find(composite(stage, key));
    return it == programs_.end() ? nullptr : &it->second;
}

ControlWord MicroprogramStore::legacyWord(uint16_t key) const {
    auto it = legacy_.find(key);
    return it == legacy_.end() ? ControlWord{} : it->second;
}

bool MicroprogramStore::hasKey(uint16_t key) const {
    return legacy_.find(key) != legacy_.end();
}

std::vector<uint16_t> MicroprogramStore::keys() const {
    std::vector<uint16_t> out;
    out.reserve(legacy_.size());
    for (const auto& entry : legacy_)
        out.push_back(entry.first);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Microcode listing
// ---------------------------------------------------------------------------

MicroprogramStore::MicroprogramStore() {
    // One fixed fetch microprogram shared by every instruction.
    {
        constexpr uint16_t kFetchKey = 0x7FFF;
        MicroProgram p;
        p.push_back(MI::transfer(BusDriver::Pc, D_FETCH_PC | D_IMEM_ADDR,
                                 "PC->IMEM_ADDR"));
        p.push_back(MI::transfer(BusDriver::ImemOut, D_IR, "IMEM->IR"));
        p.push_back(MI::transfer(BusDriver::PcPlus1, D_PC_NEXT, "PC+1->PC_NEXT"));
        p.push_back(MI::transfer(BusDriver::PcPlus1, D_PC, "PC+1->PC"));
        p.push_back(MI::commit("commit IF/ID"));
        add(Stage::Fetch, kFetchKey, p);
    }

    auto addMemory = [this](uint16_t key, bool load, bool store, bool wb,
                            bool clear) {
        add(Stage::Ma1, key, ma1Program(load || store));
        add(Stage::Ma2, key, ma2Program(load, store));
        add(Stage::Writeback, key, wbProgram(load, wb));
        add(Stage::MemoryMerged, key, mergedProgram(load, store, wb));
        (void)clear;   // clear is handled in the writeback commit
    };

    auto addBothFormats = [&](const char* mn, Signal op, bool writes,
                              MicroAluOp alu, bool unary_b,
                              bool need_rs1, bool need_rs2, bool use_a,
                              bool use_b) {
        for (bool imm : {false, true}) {
            uint16_t k = keyOf(mn, imm);
            if (writes && imm)      addLegacy(k, {op, Signal::IS_WB, Signal::IS_IMMEDIATE});
            else if (writes)        addLegacy(k, {op, Signal::IS_WB});
            else if (imm)           addLegacy(k, {op, Signal::IS_IMMEDIATE});
            else                    addLegacy(k, {op});

            add(Stage::Decode, k,
                decProgram(need_rs1, imm ? false : need_rs2, use_a,
                           imm ? false : use_b,
                           imm ? ImmKind::Signed16 : ImmKind::None));
            add(Stage::Execute, k, exAluProgram(alu, imm, unary_b));
            addMemory(k, false, false, writes, false);
        }
    };

    // ---------------- integer ALU (R and I formats) ----------------
    addBothFormats("add", Signal::IS_ADD, true,  MicroAluOp::Add, false, true, true, true, true);
    addBothFormats("sub", Signal::IS_SUB, true,  MicroAluOp::Sub, false, true, true, true, true);
    addBothFormats("mul", Signal::IS_MUL, true,  MicroAluOp::Mul, false, true, true, true, true);
    addBothFormats("div", Signal::IS_DIV, true,  MicroAluOp::Div, false, true, true, true, true);
    addBothFormats("mod", Signal::IS_MOD, true,  MicroAluOp::Mod, false, true, true, true, true);
    addBothFormats("cmp", Signal::IS_CMP, false, MicroAluOp::Cmp, false, true, true, true, true);
    addBothFormats("and", Signal::IS_AND, true,  MicroAluOp::And, false, true, true, true, true);
    addBothFormats("or",  Signal::IS_OR,  true,  MicroAluOp::Or,  false, true, true, true, true);
    addBothFormats("xor", Signal::IS_XOR, true,  MicroAluOp::Xor, false, true, true, true, true);
    addBothFormats("not", Signal::IS_NOT, true,  MicroAluOp::Not, true,  false, true, false, true);
    addBothFormats("lsl", Signal::IS_LSL, true,  MicroAluOp::Lsl, false, true, true, true, true);
    addBothFormats("lsr", Signal::IS_LSR, true,  MicroAluOp::Lsr, false, true, true, true, true);
    addBothFormats("asr", Signal::IS_ASR, true,  MicroAluOp::Asr, false, true, true, true, true);

    // ---------------- FPU (R format) ----------------
    auto addFpu = [&](const char* mn, Signal op, bool writes, MicroFpuOp fop) {
        uint16_t k = keyOf(mn, false);
        if (writes) addLegacy(k, {Signal::IS_FLOAT, op, Signal::IS_WB});
        else        addLegacy(k, {Signal::IS_FLOAT, op});
        add(Stage::Decode, k, decProgram(true, true, true, true, ImmKind::None));
        add(Stage::Execute, k, exFpuProgram(fop));
        addMemory(k, false, false, writes, false);
    };
    addFpu("fadd", Signal::IS_FADD, true,  MicroFpuOp::Fadd);
    addFpu("fsub", Signal::IS_FSUB, true,  MicroFpuOp::Fsub);
    addFpu("fmul", Signal::IS_FMUL, true,  MicroFpuOp::Fmul);
    addFpu("fdiv", Signal::IS_FDIV, true,  MicroFpuOp::Fdiv);
    addFpu("fcmp", Signal::IS_FCMP, false, MicroFpuOp::Fcmp);

    {
        uint16_t k = keyOf("fmov", false);
        addLegacy(k, {Signal::IS_FLOAT, Signal::IS_WB});
        add(Stage::Decode, k, decSimpleProgram(ImmKind::Fmov));
        add(Stage::Execute, k, exImmProgram());
        addMemory(k, false, false, true, false);
    }

    // ---------------- memory & system ----------------
    {
        uint16_t k = keyOf("ld", true);
        addLegacy(k, {Signal::IS_LD, Signal::IS_WB, Signal::IS_ADD, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decLoadProgram());
        add(Stage::Execute, k, exLoadProgram());
        addMemory(k, true, false, true, false);
    }
    {
        uint16_t k = keyOf("st", true);
        addLegacy(k, {Signal::IS_ST, Signal::IS_ADD, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decStoreProgram());
        add(Stage::Execute, k, exStoreProgram());
        addMemory(k, false, true, false, false);
    }
    {
        uint16_t k = keyOf("movu", true);
        addLegacy(k, {Signal::IS_WB, Signal::IS_MOVU, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decSimpleProgram(ImmKind::Movu));
        add(Stage::Execute, k, exImmProgram());
        addMemory(k, false, false, true, false);
    }
    {
        uint16_t k = keyOf("movh", true);
        addLegacy(k, {Signal::IS_WB, Signal::IS_MOVH, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decSimpleProgram(ImmKind::Movh));
        add(Stage::Execute, k, exImmProgram());
        addMemory(k, false, false, true, false);
    }
    {
        uint16_t k = keyOf("csrr", true);
        addLegacy(k, {Signal::IS_CSRR, Signal::IS_WB, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decSimpleProgram(ImmKind::Csr));
        add(Stage::Execute, k, exCsrrProgram());
        addMemory(k, false, false, true, false);
    }
    {
        uint16_t k = keyOf("csrw", true);
        addLegacy(k, {Signal::IS_CSRW, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decCsrwProgram());
        add(Stage::Execute, k, exCsrwProgram());
        addMemory(k, false, false, false, false);
    }
    {
        uint16_t k = keyOf("chk", false);
        addLegacy(k, {Signal::IS_CHK});
        add(Stage::Decode, k, decChkProgram());
        add(Stage::Execute, k, exChkProgram());
        addMemory(k, false, false, false, false);
    }
    {
        uint16_t k = keyOf("clrz", false);
        addLegacy(k, {Signal::IS_CLEAR, Signal::IS_WB});
        add(Stage::Decode, k, decSimpleProgram(ImmKind::None));
        add(Stage::Execute, k, exClearProgram());
        addMemory(k, false, false, true, true);
    }

    // ---------------- control flow ----------------
    auto addBranch = [&](const char* mn, Signal op, BranchCond cond) {
        uint16_t k = keyOf(mn, true);
        addLegacy(k, {op, Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decProgram(false, false, false, false, ImmKind::Branch));
        add(Stage::Execute, k, exBranchProgram(cond, false));
        addMemory(k, false, false, false, false);
    };
    addBranch("b",   Signal::IS_UBRANCH, BranchCond::Always);
    addBranch("beq", Signal::IS_BEQ,     BranchCond::Eq);
    addBranch("bne", Signal::IS_BNE,     BranchCond::Ne);
    addBranch("bgt", Signal::IS_BGT,     BranchCond::Gt);
    addBranch("blt", Signal::IS_BLT,     BranchCond::Lt);

    {
        uint16_t k = keyOf("call", true);
        addLegacy(k, {Signal::IS_CALL, Signal::IS_UBRANCH, Signal::IS_WB,
                      Signal::IS_IMMEDIATE});
        add(Stage::Decode, k, decProgram(false, false, false, false, ImmKind::Branch));
        add(Stage::Execute, k, exBranchProgram(BranchCond::Always, true));
        addMemory(k, false, false, true, false);
    }
    {
        uint16_t k = keyOf("ret", false);
        addLegacy(k, {Signal::IS_RET, Signal::IS_UBRANCH});
        add(Stage::Decode, k, decRetProgram());
        add(Stage::Execute, k, exRetProgram());
        addMemory(k, false, false, false, false);
    }
    {
        uint16_t k = keyOf("ecall", false);
        addLegacy(k, {Signal::IS_SYSCALL});
        add(Stage::Decode, k, decSyscallProgram());
        add(Stage::Execute, k, exTrapProgram());
        addMemory(k, false, false, false, false);
    }
    auto addTrap = [&](const char* mn, Signal op) {
        uint16_t k = keyOf(mn, false);
        addLegacy(k, {op});
        add(Stage::Decode, k, decCommitOnly());
        add(Stage::Execute, k, exTrapProgram());
        addMemory(k, false, false, false, false);
    };
    addTrap("eret", Signal::IS_ERET);
    addTrap("ebreak", Signal::IS_EBREAK);
    addTrap("wfi", Signal::IS_WFI);

    {
        uint16_t k = keyOf("nop", false);
        addLegacy(k, {});
        add(Stage::Decode, k, decCommitOnly());
        add(Stage::Execute, k, exCommitOnly());
        addMemory(k, false, false, false, false);
    }
}

} // namespace mc
