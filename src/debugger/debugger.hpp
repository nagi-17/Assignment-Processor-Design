
#ifndef RISC201_DEBUGGER_HPP
#define RISC201_DEBUGGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace std;


// Different ways to display a value
enum class DebugValueFormat {
    Decimal,
    Hexadecimal,
    Binary,
    Boolean
};


// Stores one value that the debugger can display
struct DebugField {
    string name;
    uint64_t value{0};
    DebugValueFormat format{DebugValueFormat::Hexadecimal};
};


// Stores information about one pipeline stage
struct PipelineStageSnapshot {
    string stage_name;
    bool is_valid{false};
    vector<DebugField> fields;
};


// Why the processor stopped
enum class DebugStopReason {
    None,
    Breakpoint,
    AddressBreakpoint,
    ProgramHalted,
    CpuError,
    Unknown
};


// Stores the processor's state at a particular moment
struct DebuggerSnapshot {
    array<uint32_t, 16> registers{};
    uint32_t pc{0};

    uint32_t epc{0};
    bool has_epc{false};

    uint32_t cause{0};
    bool has_cause{false};

    bool flag_e{false};
    bool flag_gt{false};
    bool has_flags{false};

    uint32_t micro_pc{0};
    bool has_micro_pc{false};

    vector<PipelineStageSnapshot> pipeline;

    bool is_stopped{false};
    DebugStopReason stop_reason{DebugStopReason::None};
};


// Interface between the debugger and the processor
class DebuggerTarget {
public:
    virtual ~DebuggerTarget() = default;

    // Get the current processor state
    virtual DebuggerSnapshot getSnapshot() const = 0;

    // Execute one processor cycle
    virtual bool stepCycle() = 0;

    // Continue processor execution
    virtual bool resume() = 0;

    // Read one memory word
    virtual bool readMemoryWord(uint32_t address, uint32_t& value) const = 0;
};


// Get a register's value
bool getRegisterValue(const DebuggerSnapshot& snapshot, size_t registerIndex,
                      uint32_t& value);

// Find a pipeline stage by its name
const PipelineStageSnapshot* findPipelineStage(
    const DebuggerSnapshot& snapshot, const string& stageName);

// Find a particular field inside a pipeline stage
const DebugField* findPipelineField(
    const DebuggerSnapshot& snapshot,
    const string& stageName,
    const string& fieldName);

// Get a pipeline field's value and its display format
bool getPipelineFieldValue(
    const DebuggerSnapshot& snapshot,
    const string& stageName,
    const string& fieldName,
    uint64_t& value,
    DebugValueFormat& format);

// Check whether execution stopped because of a breakpoint
bool isBreakpointStop(const DebuggerSnapshot& snapshot);

// Convert a value into decimal, hexadecimal, binary, or boolean text
string formatValue(uint64_t value, DebugValueFormat format);

// Convert the stop reason into readable text
string stopReasonName(DebugStopReason reason);

// Convert text such as "25" or "0x19" into a number
bool parseNumber(const string& text, uint64_t& value);

// Convert a register name or number into a register index
bool parseRegisterIndex(string name, size_t& index);

// Convert a memory address into hexadecimal text
string hexAddress(uint64_t address);

#endif
