#ifndef CONTROL_STATUS_UNIT_HPP
#define CONTROL_STATUS_UNIT_HPP

// System Registers (Inaccessible directly by the programmer:
//   flags.E / flags.GT  - comparison flags set by cmp / fcmp
//   epc                 - exception program counter
//   cause               - exception cause code
// and implements the system-level behaviour of csrr, csrw, ecall, ebreak, chk,
// eret and wfi.

// PC values (including epc and the trap vector) are in INSTRUCTION-INDEX units,
// matching InstructionFetchStage (pc_next = pc + 1) and instruction_decode.cpp
// (which converts the assembler's byte offsets to indices).

#include <cstdint>

// csr_imm encodings for csrr / csrw
enum class CsrIndex : uint32_t {
    Flags = 0,   // bit 0 = E (equal), bit 1 = GT (greater-than)
    Epc   = 1,   // exception program counter
    Cause = 2    // exception cause code
};

// exception cause codes stored in the `cause` register.
enum class ExceptionCause : uint32_t {
    None               = 0,
    Syscall            = 1,   // ecall
    Breakpoint         = 2,   // ebreak
    OutOfBounds        = 3,   // chk failed (rs1 >= rs2)
    IllegalInstruction = 4    // opcode the control store does not know
};

struct StatusInputs {
    bool     flag_write{false};    // cmp and fcmp produced a flag result
    bool     flag_equal{false};
    bool     flag_greater{false};

    bool     is_ecall{false};      // IS_SYSCALL
    bool     is_ebreak{false};     // IS_EBREAK
    bool     is_eret{false};       // IS_ERET
    bool     is_wfi{false};        // IS_WFI
    bool     chk_fault{false};     // chk failed -> OUT_OF_BOUNDS
    uint32_t pc{0};                // instruction index of the EX instruction
};

struct StatusSignals {
    bool     is_trap{false};
    uint32_t trap_vector{0};
    bool     is_eret{false};
    uint32_t epc{0};
    bool     halted{false};        // wfi: fetch stays stalled until wake()
};

class ControlStatusUnit {
public:
    // trap handler entry point
    static constexpr uint32_t kDefaultTrapVector = 0x20;

    explicit ControlStatusUnit(uint32_t trap_vector = kDefaultTrapVector);

    // csrr / csrw - called from EX unit
    // read CSR at `csr_index`, returns false for a reserved index, leaving `value` untouched.
    bool readCsr(uint32_t csr_index, uint32_t& value) const;

    // write `value` into the CSR at `csr_index`, returns false for a reserved index, leaving all state untouched.
    bool writeCsr(uint32_t csr_index, uint32_t value);

    // record a trap to be taken on the next clockNext() - (used for the illegal
    // opcode polled from the decode stage, and directly by tests)
    void raiseTrap(ExceptionCause cause, uint32_t epc);

    // one clock edge - latches flag/wfi/trap/eret inputs and produces the fetch
    // redirect signals for this cycle.
    StatusSignals clockNext(const StatusInputs& in);

    uint32_t epc() const          { return epc_; }
    uint32_t cause() const        { return cause_; }
    bool     flagEqual() const    { return flag_e_; }
    bool     flagGreater() const  { return flag_gt_; }
    bool     halted() const       { return halted_; }
    bool     inTrap() const       { return in_trap_; }
    uint32_t trapVector() const   { return trap_vector_; }

    // etx interrupt - releases wfi.
    void wake()  { halted_ = false; }

    // power-on
    void reset();

private:
    uint32_t trap_vector_;

    uint32_t epc_{0};
    uint32_t cause_{0};
    bool     flag_e_{false};
    bool     flag_gt_{false};
    bool     halted_{false};
    bool     in_trap_{false};

    bool           pending_{false};
    ExceptionCause pending_cause_{ExceptionCause::None};
    uint32_t       pending_epc_{0};
};

#endif
