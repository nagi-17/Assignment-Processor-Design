#include "debugger.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

using namespace std;

bool getRegisterValue(const DebuggerSnapshot& snapshot, size_t registerIndex, uint32_t& value) {
    if (registerIndex >= snapshot.registers.size()) {
        return false;
    }

    value = snapshot.registers[registerIndex];
    return true;
}

const PipelineStageSnapshot* findPipelineStage(const DebuggerSnapshot& snapshot, const string& stageName) {
    for (const PipelineStageSnapshot& stage : snapshot.pipeline) {
        if (stage.stage_name == stageName) {
            return &stage;
        }
    }

    return nullptr;
}

const DebugField* findPipelineField(const DebuggerSnapshot& snapshot,
                                    const string& stageName,
                                    const string& fieldName) {
    const PipelineStageSnapshot* stage = findPipelineStage(snapshot, stageName);
    if (stage == nullptr) {
        return nullptr;
    }

    for (const DebugField& field : stage->fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }

    return nullptr;
}

bool getPipelineFieldValue(const DebuggerSnapshot& snapshot,
                           const string& stageName,
                           const string& fieldName,
                           uint64_t& value,
                           DebugValueFormat& format) {
    const DebugField* field = findPipelineField(snapshot, stageName, fieldName);
    if (field == nullptr) {
        return false;
    }

    value = field->value;
    format = field->format;
    return true;
}

bool isBreakpointStop(const DebuggerSnapshot& snapshot) {
    return snapshot.is_stopped &&
           (snapshot.stop_reason == DebugStopReason::Breakpoint ||
            snapshot.stop_reason == DebugStopReason::AddressBreakpoint);
}

string formatValue(uint64_t value, DebugValueFormat format) {
    ostringstream output;

    switch (format) {
        case DebugValueFormat::Decimal:
            output << value;
            break;
        case DebugValueFormat::Hexadecimal:
            output << "0x" << hex << nouppercase << value;
            break;
        case DebugValueFormat::Binary: {
            string bits;
            do {
                bits.insert(bits.begin(), (value & 1) ? '1' : '0');
                value >>= 1;
            } while (value != 0);
            output << "0b" << bits;
            break;
        }
        case DebugValueFormat::Boolean:
            output << (value ? "true" : "false");
            break;
    }

    return output.str();
}

string stopReasonName(DebugStopReason reason) {
    switch (reason) {
        case DebugStopReason::None: return "none";
        case DebugStopReason::Breakpoint: return "breakpoint (ebreak)";
        case DebugStopReason::AddressBreakpoint: return "debugger address breakpoint";
        case DebugStopReason::ProgramHalted: return "program halted";
        case DebugStopReason::CpuError: return "CPU error";
        case DebugStopReason::Unknown: return "unknown";
    }

    return "unknown";
}

bool parseNumber(const string& text, uint64_t& value) {
    if (text.empty() || text[0] == '-' || text[0] == '+') {
        return false;
    }

    // Accept plain decimal numbers and 0x-prefixed hexadecimal numbers.
    // Treat strings such as 08 as decimal eight, not invalid octal input.
    int base = 10;
    string digits = text;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        digits = text.substr(2);
    }

    if (digits.empty()) {
        return false;
    }

    try {
        size_t used = 0;
        unsigned long long parsed = stoull(digits, &used, base);
        if (used != digits.size()) {
            return false;
        }
        value = static_cast<uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseRegisterIndex(string name, size_t& index) {
    if (!name.empty() && (name[0] == 'r' || name[0] == 'R')) {
        name.erase(0, 1);
    }

    uint64_t parsed = 0;
    if (!parseNumber(name, parsed) || parsed >= 16) {
        return false;
    }

    index = static_cast<size_t>(parsed);
    return true;
}

string hexAddress(uint64_t address) {
    ostringstream output;
    output << "0x" << hex << nouppercase << address;
    return output.str();
}
