#include "debugger.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    DebuggerSnapshot snapshot;
    snapshot.registers[5] = 0x1234ABCD;
    snapshot.pipeline.push_back({"ID/EX", true, {
        {"OP1", 17, DebugValueFormat::Decimal},
        {"IS_ADD", 1, DebugValueFormat::Boolean}
    }});

    std::uint32_t registerValue = 0;
    assert(getRegisterValue(snapshot, 5, registerValue));
    assert(registerValue == 0x1234ABCD);
    assert(!getRegisterValue(snapshot, 16, registerValue));

    const PipelineStageSnapshot* stage = findPipelineStage(snapshot, "ID/EX");
    assert(stage != nullptr && stage->is_valid);
    assert(findPipelineStage(snapshot, "UNKNOWN") == nullptr);

    const DebugField* field = findPipelineField(snapshot, "ID/EX", "OP1");
    assert(field != nullptr && field->value == 17);
    assert(findPipelineField(snapshot, "ID/EX", "MISSING") == nullptr);

    std::uint64_t fieldValue = 0;
    DebugValueFormat format = DebugValueFormat::Hexadecimal;
    assert(getPipelineFieldValue(snapshot, "ID/EX", "IS_ADD", fieldValue, format));
    assert(fieldValue == 1 && format == DebugValueFormat::Boolean);

    assert(formatValue(255, DebugValueFormat::Hexadecimal) == "0xff");
    assert(formatValue(10, DebugValueFormat::Decimal) == "10");
    assert(formatValue(5, DebugValueFormat::Binary) == "0b101");
    assert(formatValue(0, DebugValueFormat::Boolean) == "false");
    assert(formatValue(1, DebugValueFormat::Boolean) == "true");

    std::uint64_t parsed = 0;
    assert(parseNumber("123", parsed) && parsed == 123);
    assert(parseNumber("0x100", parsed) && parsed == 256);
    assert(parseNumber("08", parsed) && parsed == 8);
    assert(!parseNumber("-1", parsed));
    assert(!parseNumber("0x", parsed));
    assert(!parseNumber("12xyz", parsed));

    std::size_t registerIndex = 0;
    assert(parseRegisterIndex("r5", registerIndex) && registerIndex == 5);
    assert(parseRegisterIndex("R15", registerIndex) && registerIndex == 15);
    assert(parseRegisterIndex("3", registerIndex) && registerIndex == 3);
    assert(!parseRegisterIndex("r16", registerIndex));

    assert(hexAddress(256) == "0x100");
    snapshot.is_stopped = true;
    snapshot.stop_reason = DebugStopReason::Breakpoint;
    assert(isBreakpointStop(snapshot));
    snapshot.stop_reason = DebugStopReason::AddressBreakpoint;
    assert(isBreakpointStop(snapshot));
    assert(stopReasonName(DebugStopReason::ProgramHalted) == "program halted");

    std::cout << "Debugger helper tests passed.\n";
    return 0;
}
