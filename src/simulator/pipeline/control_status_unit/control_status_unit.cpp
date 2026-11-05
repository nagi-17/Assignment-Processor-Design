#include "control_status_unit.hpp"

namespace {
    constexpr uint32_t csrId(CsrIndex index) {
        return static_cast<uint32_t>(index);
    }

    constexpr uint32_t causeId(ExceptionCause cause) {
        return static_cast<uint32_t>(cause);
    }
}

ControlStatusUnit::ControlStatusUnit(uint32_t trap_vector)
    : trap_vector_(trap_vector) {}

bool ControlStatusUnit::readCsr(uint32_t csr_index, uint32_t& value) const {
    if (csr_index == csrId(CsrIndex::Flags)) {
        value = (flag_gt_ ? 2u : 0u) | (flag_e_ ? 1u : 0u);
        return true;
    }
    if (csr_index == csrId(CsrIndex::Epc)) {
        value = epc_;
        return true;
    }
    if (csr_index == csrId(CsrIndex::Cause)) {
        value = cause_;
        return true;
    }
    return false;
}

bool ControlStatusUnit::writeCsr(uint32_t csr_index, uint32_t value) {
    if (csr_index == csrId(CsrIndex::Flags)) {
        flag_e_  = (value & 0x1u) != 0;
        flag_gt_ = ((value >> 1) & 0x1u) != 0;
        return true;
    }
    if (csr_index == csrId(CsrIndex::Epc)) {
        epc_ = value;
        return true;
    }
    if (csr_index == csrId(CsrIndex::Cause)) {
        cause_ = value;
        return true;
    }
    return false;
}

void ControlStatusUnit::raiseTrap(ExceptionCause cause, uint32_t epc) {
    pending_       = true;
    pending_cause_ = cause;
    pending_epc_   = epc;
}

StatusSignals ControlStatusUnit::clockNext(const StatusInputs& in) {
    // flags are updated independently of any redirect happening this cycle
    if (in.flag_write) {
        flag_e_  = in.flag_equal;
        flag_gt_ = in.flag_greater;
    }

    // wfi parks the core until an external interrupt calls wake()
    if (in.is_wfi) {
        halted_ = true;
    }

    StatusSignals out;
    out.halted = halted_;

    // trap sources, highest priority first - a pending trap (illegal opcode)
    // outranks ecall, which outranks ebreak, which outranks a chk failure
    ExceptionCause cause    = ExceptionCause::None;
    uint32_t       fault_pc = 0;

    if (pending_) {
        cause    = pending_cause_;
        fault_pc = pending_epc_;
    } else if (in.is_ecall) {
        cause    = ExceptionCause::Syscall;
        fault_pc = in.pc;
    } else if (in.is_ebreak) {
        cause    = ExceptionCause::Breakpoint;
        fault_pc = in.pc;
    } else if (in.chk_fault) {
        cause    = ExceptionCause::OutOfBounds;
        fault_pc = in.pc;
    }

    if (cause != ExceptionCause::None) {
        pending_ = false;

        // imp - traps are single level: while a handler is running, new traps are
        // dropped so a faulting handler cannot nest forever
        if (in_trap_) {
            return out;
        }

        epc_     = fault_pc;
        cause_   = causeId(cause);
        in_trap_ = true;

        out.is_trap     = true;
        out.trap_vector = trap_vector_;
        return out;
    }

    // eret: return from the handler, PC <- epc - only way out of a trap,
    // so a trap raised while handling one is never silently escaped
    if (in.is_eret) {
        out.is_eret = true;
        out.epc     = epc_;
        in_trap_    = false;
    }

    return out;
}

void ControlStatusUnit::reset() {
    epc_           = 0;
    cause_         = causeId(ExceptionCause::None);
    flag_e_        = false;
    flag_gt_       = false;
    halted_        = false;
    in_trap_       = false;
    pending_       = false;
    pending_cause_ = ExceptionCause::None;
    pending_epc_   = 0;
}
