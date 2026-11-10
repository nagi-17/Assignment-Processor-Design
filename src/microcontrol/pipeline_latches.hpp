#ifndef MICROCONTROL_PIPELINE_LATCHES_HPP
#define MICROCONTROL_PIPELINE_LATCHES_HPP

// ---------------------------------------------------------------------------
// Pipeline latches for the microcontrol model.
//
// These are new, additive latch types.  They carry the same architectural
// fields as the production latches plus the 7-bit micro_key that selects the
// stage-local microprogram for the instruction in flight.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "control_signals.hpp"
#include "microinstruction.hpp"

namespace mc {

constexpr uint16_t kNoMicroKey = 0xFFFF;

struct LatchIFID {
    uint32_t pc{0};
    uint32_t ir{0};
    uint32_t pc_next{0};
    bool     valid{false};

    void reset() {
        pc = 0; ir = 0; pc_next = 0; valid = false;
    }
};

struct LatchIDEX {
    uint16_t     micro_key{kNoMicroKey};
    uint32_t     pc_next{0};
    uint8_t      rd{0};
    uint32_t     a{0};
    uint32_t     b{0};
    uint32_t     imm{0};
    ControlWord  control{};
    bool         valid{false};

    void reset() {
        micro_key = kNoMicroKey;
        pc_next = 0;
        rd = 0;
        a = b = imm = 0;
        control.reset();
        valid = false;
    }
};

// Used for EX/MEM, MA1/MA2 and MA2/WB in the 6-stage pipeline, and for the
// merged EX/MEM/WB latch in the 4-stage pipeline.
struct LatchMem {
    uint16_t micro_key{kNoMicroKey};
    uint32_t result{0};
    uint32_t st_data{0};
    uint32_t pc_next{0};
    uint32_t branch_target{0};
    uint32_t read_data{0};
    uint8_t  rd{0};
    bool     is_ld{false};
    bool     is_st{false};
    bool     is_wb{false};
    bool     is_clear{false};
    bool     is_call{false};
    bool     is_ecall{false};
    bool     is_ebreak{false};
    bool     is_eret{false};
    bool     is_wfi{false};
    bool     stack_fault{false};
    bool     branch_taken{false};
    bool     valid{false};

    void reset() {
        micro_key = kNoMicroKey;
        result = st_data = pc_next = branch_target = read_data = 0;
        rd = 0;
        is_ld = is_st = is_wb = is_clear = is_call = false;
        is_ecall = is_ebreak = is_eret = is_wfi = false;
        stack_fault = false;
        branch_taken = false;
        valid = false;
    }
};

// Simple instruction memory for the model (binary strings, one per line).
class MicroInstructionMemory {
public:
    void load(const std::vector<std::string>& program) { memory_ = program; }
    std::size_t size() const { return memory_.size(); }

    bool read(uint32_t index, uint32_t& ir) const {
        if (index >= memory_.size()) return false;
        const std::string& s = memory_[index];
        if (s.size() < 32) return false;
        uint32_t v = 0;
        for (int i = 0; i < 32; ++i) {
            if (s[i] != '0' && s[i] != '1') return false;
            v = (v << 1) | (s[i] == '1' ? 1u : 0u);
        }
        ir = v;
        return true;
    }

private:
    std::vector<std::string> memory_;
};

} // namespace mc

#endif // MICROCONTROL_PIPELINE_LATCHES_HPP
