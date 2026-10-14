#ifndef RISC201_DEBUGGER_HPP
#define RISC201_DEBUGGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class DebugValueFormat {
    Decimal,
    Hexadecimal,
    Binary,
    Boolean
};

struct DebugField {
    std::string name;
    std::uint64_t value{0};
    DebugValueFormat format{DebugValueFormat::Hexadecimal};
};

struct PipelineStageSnapshot {
    std::string stage_name;
    bool is_valid{false};
    std::vector<DebugField> fields;
};

enum class DebugStopReason {
    None,
    Breakpoint,          // CPU-reported stop, for example ebreak
    AddressBreakpoint,   // CLI breakpoint at a program address
    ProgramHalted,
    CpuError,
    Unknown
};

// One consistent, pipeline-independent view of the CPU for the CLI.
struct DebuggerSnapshot {
    std::array<std::uint32_t, 16> registers{};
    std::uint32_t pc{0};

    std::uint32_t epc{0};
    bool has_epc{false};
    std::uint32_t cause{0};
    bool has_cause{false};
    bool flag_e{false};
    bool flag_gt{false};
    bool has_flags{false};
    std::uint32_t micro_pc{0};
    bool has_micro_pc{false};

    std::vector<PipelineStageSnapshot> pipeline;
    bool is_stopped{false};
    DebugStopReason stop_reason{DebugStopReason::None};
};

// The CLI only knows this small contract. A real CPU can implement it directly,
// or be wrapped by CallbackDebuggerTarget (see debugger_callback_target.hpp).
class DebuggerTarget {
public:
    virtual ~DebuggerTarget() = default;
    virtual DebuggerSnapshot getSnapshot() const = 0;
    virtual bool stepCycle() = 0;
    virtual bool resume() = 0;
    virtual bool readMemoryWord(std::uint32_t address, std::uint32_t& value) const = 0;
};

bool getRegisterValue(const DebuggerSnapshot& snapshot, std::size_t registerIndex,
                      std::uint32_t& value);
const PipelineStageSnapshot* findPipelineStage(const DebuggerSnapshot& snapshot,
                                                const std::string& stageName);
const DebugField* findPipelineField(const DebuggerSnapshot& snapshot,
                                   const std::string& stageName,
                                   const std::string& fieldName);
bool getPipelineFieldValue(const DebuggerSnapshot& snapshot, const std::string& stageName,
                           const std::string& fieldName, std::uint64_t& value,
                           DebugValueFormat& format);
bool isBreakpointStop(const DebuggerSnapshot& snapshot);
std::string formatValue(std::uint64_t value, DebugValueFormat format);
std::string stopReasonName(DebugStopReason reason);
bool parseNumber(const std::string& text, std::uint64_t& value);
bool parseRegisterIndex(std::string name, std::size_t& index);
std::string hexAddress(std::uint64_t address);


#endif // RISC201_DEBUGGER_HPP
