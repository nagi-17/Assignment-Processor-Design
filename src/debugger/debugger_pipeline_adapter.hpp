#ifndef RISC201_DEBUGGER_PIPELINE_ADAPTER_HPP
#define RISC201_DEBUGGER_PIPELINE_ADAPTER_HPP

#include "debugger.hpp"
#include "control_signals.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace debugger_adapter_detail {

inline DebugField field(
    const string& name,
    uint64_t value,
    DebugValueFormat format = DebugValueFormat::Hexadecimal) {

    return DebugField{name, value, format};
}

inline uint32_t addressValue(uint32_t value, uint32_t pcScale) {
    return static_cast<uint32_t>(
        static_cast<uint64_t>(value) * pcScale
    );
}

inline void appendControlSignals(
    vector<DebugField>& fields,
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

    for (size_t i = 0; i < CONTROL_WORD_WIDTH; ++i) {
        fields.push_back(
            field(names[i], control.bits.test(i), DebugValueFormat::Boolean)
        );
    }
}

inline DebugField flag(const char* name, bool value) {
    return field(
        name,
        value ? 1U : 0U,
        DebugValueFormat::Boolean
    );
}

}

inline void setDebuggerPC(
    DebuggerSnapshot& snapshot,
    uint32_t sourcePC,
    uint32_t pcScale = 1) {

    snapshot.pc = debugger_adapter_detail::addressValue(
        sourcePC, pcScale
    );
}

template <typename IFID, typename IDEX, typename EXMW>
void appendFourStagePipeline(
    DebuggerSnapshot& snapshot,
    const IFID& ifid,
    const IDEX& idex,
    const EXMW& exmwb,
    uint32_t pcScale = 1) {

    using debugger_adapter_detail::field;
    using debugger_adapter_detail::flag;
    using debugger_adapter_detail::addressValue;

    snapshot.pipeline.clear();

    snapshot.pipeline.push_back({
        "IF/ID", ifid.is_valid, {
            field("PC", addressValue(ifid.pc, pcScale)),
            field("IR", ifid.ir),
            field("PC_NEXT", addressValue(ifid.pc_next, pcScale))
        }
    });

    PipelineStageSnapshot decode{
        "ID/EX", idex.is_valid, {
            field("PC_NEXT", addressValue(idex.pc_next, pcScale)),
            field("RD", idex.rd, DebugValueFormat::Decimal),
            field("OP1", idex.op1),
            field("OP2", idex.op2),
            field("IMMX", idex.immx)
        }
    };

    debugger_adapter_detail::appendControlSignals(
        decode.fields, idex.control
    );

    snapshot.pipeline.push_back(move(decode));

    snapshot.pipeline.push_back({
        "EX/MWB", exmwb.is_valid, {
            field("EXRESULT", exmwb.exResult),
            field("ST_DATA", exmwb.st_data),
            field("PC_NEXT", addressValue(exmwb.pc_next, pcScale)),
            field("RD", exmwb.rd, DebugValueFormat::Decimal),
            flag("IS_LD", exmwb.is_Ld),
            flag("IS_ST", exmwb.is_St),
            flag("IS_WB", exmwb.is_Wb),
            flag("IS_CLEAR", exmwb.is_Clear),
            flag("IS_CALL", exmwb.is_Call),
            flag("IS_ECALL", exmwb.is_Ecall),
            flag("IS_EBREAK", exmwb.is_Ebreak),
            flag("IS_ERET", exmwb.is_Eret),
            flag("IS_WFI", exmwb.is_Wfi)
        }
    });
}

template <
    typename IFID,
    typename IDEX,
    typename EXMA1,
    typename MA1MA2,
    typename MA2WB
>
void appendSixStagePipeline(
    DebuggerSnapshot& snapshot,
    const IFID& ifid,
    const IDEX& idex,
    const EXMA1& exma1,
    const MA1MA2& ma1ma2,
    const MA2WB& ma2wb,
    uint32_t pcScale = 1) {

    using debugger_adapter_detail::field;
    using debugger_adapter_detail::flag;
    using debugger_adapter_detail::addressValue;

    snapshot.pipeline.clear();

    snapshot.pipeline.push_back({
        "IF/ID", ifid.is_valid, {
            field("PC", addressValue(ifid.pc, pcScale)),
            field("IR", ifid.ir),
            field("PC_NEXT", addressValue(ifid.pc_next, pcScale))
        }
    });

    PipelineStageSnapshot decode{
        "ID/EX", idex.is_valid, {
            field("PC_NEXT", addressValue(idex.pc_next, pcScale)),
            field("RD", idex.rd, DebugValueFormat::Decimal),
            field("OP1", idex.op1),
            field("OP2", idex.op2),
            field("IMMX", idex.immx)
        }
    };

    debugger_adapter_detail::appendControlSignals(
        decode.fields, idex.control
    );

    snapshot.pipeline.push_back(move(decode));

    snapshot.pipeline.push_back({
        "EX/MA1", exma1.is_valid, {
            field("EXRESULT", exma1.exResult),
            field("ST_DATA", exma1.st_data),
            field("RD", exma1.rd, DebugValueFormat::Decimal),
            flag("IS_LD", exma1.is_Ld),
            flag("IS_ST", exma1.is_St),
            flag("IS_WB", exma1.is_Wb),
            flag("IS_CLEAR", exma1.is_Clear),
            flag("IS_ECALL", exma1.is_Ecall),
            flag("IS_EBREAK", exma1.is_Ebreak
