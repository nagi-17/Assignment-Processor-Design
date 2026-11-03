#ifndef RISC201_DEBUGGER_PIPELINE_ADAPTER_HPP
#define RISC201_DEBUGGER_PIPELINE_ADAPTER_HPP

#include "debugger.hpp"
#include "control_signals.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The adapter intentionally does NOT include either latch header. Both current
// latch headers declare classes named IF_ID_Latch and ID_EX_Latch, so a single
// translation unit must select one pipeline design. Include the desired latch
// header in the .cpp that calls these templates.
namespace debugger_adapter_detail {

inline DebugField field(const std::string& name, std::uint64_t value,
                        DebugValueFormat format = DebugValueFormat::Hexadecimal) {
    return DebugField{name, value, format};
}

inline std::uint32_t addressValue(std::uint32_t value, std::uint32_t pcScale) {
    // pcScale is 1 when latch PC fields already use byte addresses, or 4 when
    // the fetch component currently stores an instruction index.
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) * pcScale);
}

inline void appendControlSignals(std::vector<DebugField>& fields,
                                 const ControlSignals& control) {
    static const char* names[CONTROL_WORD_WIDTH] = {
        "IS_ST", "IS_LD", "IS_BEQ", "IS_BGT", "IS_BLT", "IS_BNE",
        "IS_RET", "IS_IMMEDIATE", "IS_WB", "IS_UBRANCH", "IS_CALL",
        "IS_ADD", "IS_SUB", "IS_CMP", "IS_MUL", "IS_DIV", "IS_MOD",
        "IS_LSL", "IS_LSR", "IS_ASR", "IS_OR", "IS_AND", "IS_NOT",
        "IS_XOR", "IS_WFI", "IS_ERET", "IS_CSRR", "IS_CSRW",
        "IS_SYSCALL", "IS_EBREAK", "IS_FLOAT", "IS_FADD", "IS_FSUB",
        "IS_FMUL", "IS_FDIV", "IS_FCMP", "IS_CLEAR"
    };

    for (std::size_t i = 0; i < CONTROL_WORD_WIDTH; ++i) {
        fields.push_back(field(names[i], control.bits.test(i), DebugValueFormat::Boolean));
    }
}

inline DebugField flag(const char* name, bool value) {
    return field(name, value ? 1U : 0U, DebugValueFormat::Boolean);
}

} // namespace debugger_adapter_detail

// Scales the architectural PC only when the CPU/fetch code uses instruction
// indices. Do not use a scale of 4 until the team agrees that its public PC unit
// is an index; the current instruction_fetch.cpp does use pc + 1 internally.
inline void setDebuggerPC(DebuggerSnapshot& snapshot, std::uint32_t sourcePC,
                          std::uint32_t pcScale = 1) {
    snapshot.pc = debugger_adapter_detail::addressValue(sourcePC, pcScale);
}

// Works with the existing four-stage latch types when the including .cpp has
// included four_stage_latches.hpp.
template <typename IFID, typename IDEX, typename EXMW>
void appendFourStagePipeline(DebuggerSnapshot& snapshot, const IFID& ifid,
                             const IDEX& idex, const EXMW& exmwb,
                             std::uint32_t pcScale = 1) {
    using debugger_adapter_detail::field;
    using debugger_adapter_detail::flag;
    using debugger_adapter_detail::addressValue;

    snapshot.pipeline.clear();
    snapshot.pipeline.push_back({"IF/ID", ifid.is_valid, {
        field("PC", addressValue(ifid.pc, pcScale)),
        field("IR", ifid.ir),
        field("PC_NEXT", addressValue(ifid.pc_next, pcScale))
    }});

    PipelineStageSnapshot decode{"ID/EX", idex.is_valid, {
        field("PC_NEXT", addressValue(idex.pc_next, pcScale)),
        field("RD", idex.rd, DebugValueFormat::Decimal),
        field("OP1", idex.op1), field("OP2", idex.op2), field("IMMX", idex.immx)
    }};
    debugger_adapter_detail::appendControlSignals(decode.fields, idex.control);
    snapshot.pipeline.push_back(std::move(decode));

    snapshot.pipeline.push_back({"EX/MWB", exmwb.is_valid, {
        field("EXRESULT", exmwb.exResult), field("ST_DATA", exmwb.st_data),
        field("PC_NEXT", addressValue(exmwb.pc_next, pcScale)),
        field("RD", exmwb.rd, DebugValueFormat::Decimal),
        flag("IS_LD", exmwb.is_Ld), flag("IS_ST", exmwb.is_St),
        flag("IS_WB", exmwb.is_Wb), flag("IS_CLEAR", exmwb.is_Clear),
        flag("IS_CALL", exmwb.is_Call), flag("IS_ECALL", exmwb.is_Ecall),
        flag("IS_EBREAK", exmwb.is_Ebreak), flag("IS_ERET", exmwb.is_Eret),
        flag("IS_WFI", exmwb.is_Wfi)
    }});
}

// Works with the existing six-stage latch types when the including .cpp has
// included six_stage_latches.hpp. These stage names match the repository.
template <typename IFID, typename IDEX, typename EXMA1, typename MA1MA2, typename MA2WB>
void appendSixStagePipeline(DebuggerSnapshot& snapshot, const IFID& ifid,
                            const IDEX& idex, const EXMA1& exma1,
                            const MA1MA2& ma1ma2, const MA2WB& ma2wb,
                            std::uint32_t pcScale = 1) {
    using debugger_adapter_detail::field;
    using debugger_adapter_detail::flag;
    using debugger_adapter_detail::addressValue;

    snapshot.pipeline.clear();
    snapshot.pipeline.push_back({"IF/ID", ifid.is_valid, {
        field("PC", addressValue(ifid.pc, pcScale)),
        field("IR", ifid.ir),
        field("PC_NEXT", addressValue(ifid.pc_next, pcScale))
    }});

    PipelineStageSnapshot decode{"ID/EX", idex.is_valid, {
        field("PC_NEXT", addressValue(idex.pc_next, pcScale)),
        field("RD", idex.rd, DebugValueFormat::Decimal),
        field("OP1", idex.op1), field("OP2", idex.op2), field("IMMX", idex.immx)
    }};
    debugger_adapter_detail::appendControlSignals(decode.fields, idex.control);
    snapshot.pipeline.push_back(std::move(decode));

    snapshot.pipeline.push_back({"EX/MA1", exma1.is_valid, {
        field("EXRESULT", exma1.exResult), field("ST_DATA", exma1.st_data),
        field("RD", exma1.rd, DebugValueFormat::Decimal),
        flag("IS_LD", exma1.is_Ld), flag("IS_ST", exma1.is_St),
        flag("IS_WB", exma1.is_Wb), flag("IS_CLEAR", exma1.is_Clear),
        flag("IS_ECALL", exma1.is_Ecall),
        flag("IS_EBREAK", exma1.is_Ebreak), flag("IS_ERET", exma1.is_Eret),
        flag("IS_WFI", exma1.is_Wfi)
    }});

    snapshot.pipeline.push_back({"MA1/MA2", ma1ma2.is_valid, {
        field("EXRESULT", ma1ma2.exResult), field("ST_DATA", ma1ma2.st_data),
        field("RD", ma1ma2.rd, DebugValueFormat::Decimal),
        flag("IS_LD", ma1ma2.is_Ld), flag("IS_ST", ma1ma2.is_St),
        flag("IS_WB", ma1ma2.is_Wb), flag("IS_CLEAR", ma1ma2.is_Clear),
        flag("STACK_FAULT", ma1ma2.stack_fault),
        flag("IS_ECALL", ma1ma2.is_Ecall), flag("IS_EBREAK", ma1ma2.is_Ebreak),
        flag("IS_ERET", ma1ma2.is_Eret), flag("IS_WFI", ma1ma2.is_Wfi)
    }});

    snapshot.pipeline.push_back({"MA2/WB", ma2wb.is_valid, {
        field("EXRESULT", ma2wb.exResult), field("READ_DATA", ma2wb.read_data),
        field("RD", ma2wb.rd, DebugValueFormat::Decimal),
        flag("IS_LD", ma2wb.is_Ld), flag("IS_WB", ma2wb.is_Wb),
        flag("IS_CLEAR", ma2wb.is_Clear)
    }});
}

#endif // RISC201_DEBUGGER_PIPELINE_ADAPTER_HPP
