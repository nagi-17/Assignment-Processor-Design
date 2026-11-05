#ifndef FOUR_STAGE_LATCHES_HPP
#define FOUR_STAGE_LATCHES_HPP

#include <cstdint>
#include <iostream>
#include <iomanip>
#include "control_signals.hpp"

class IF_ID_Latch
{
    public:
        uint32_t pc{0};       // Program Counter (required for branch target in ID)
        uint32_t ir{0};       // Complete instruction register
        uint32_t pc_next{0};  // PC + 4 for function calls and traps
        bool is_valid{false}; // Bubble flag for stalls and flushes

        void reset()
        {
            pc = 0;
            ir = 0;
            pc_next = 0;
            is_valid = false;
        }

        void flush()
        {
            reset(); // Clear latch state on branch flush or exception
        }

        void dump() const
        {
            std::cout << "[IF/ID Latch] "
                    << "Valid: " << is_valid
                    << " | PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << pc
                    << " | IR: 0x" << std::setw(8) << ir
                    << " | PC_Next: 0x" << std::setw(8) << pc_next << std::dec << "\n";
        }
};

class ID_EX_Latch
{
    public:
        ControlSignals control; // 37-bit control signals bundle
        uint32_t pc_next{0};    // PC + 4 value
        bool is_valid{false};   // Bubble flag
        uint8_t rd{0};          // Target destination register address (4 bits)
        uint32_t op1{0};        // RS1 operand data for ALU/FPU
        uint32_t op2{0};        // RS2 operand data for ALU/FPU
        uint32_t immx{0};       // Modified sign-extended immediate value

        void reset()
        {
            control.reset();
            pc_next = 0;
            is_valid = false;
            rd = 0;
            op1 = 0;
            op2 = 0;
            immx = 0;
        }

        void flush()
        {
            reset();
        }

        void dump() const
        {
            std::cout << "[ID/EX Latch] "
                    << "Valid: " << is_valid
                    << " | rd: r" << static_cast<int>(rd & 0x0F) // Masked to 4 bits
                    << " | OP1: 0x" << std::hex << std::setw(8) << std::setfill('0') << op1
                    << " | OP2: 0x" << std::setw(8) << op2
                    << " | IMMX: 0x" << std::setw(8) << immx
                    << " | PC_Next: 0x" << std::setw(8) << pc_next << std::dec << "\n";
        }
};

class EX_MWB_Latch
{
    public:
        uint32_t exResult{0};  // Computed ALU/FPU result or memory target address (mar)
        uint32_t st_data{0};   // Store payload (mdr)
        uint32_t pc_next{0};   // PC + 4 link address for call instructions

        // Control Signals
        bool is_Ld{false};
        bool is_St{false};
        bool is_Wb{false};
        bool is_Clear{false};
        bool is_Call{false};
        uint8_t rd{0};
        bool is_valid{false};

        // Traps
        bool is_Ecall{false};
        bool is_Ebreak{false};
        bool is_Eret{false};
        bool is_Wfi{false};

        void reset() {
            exResult = 0; st_data = 0; pc_next = 0;
            is_Ld = false; is_St = false; is_Wb = false; is_Clear = false; is_Call = false;
            rd = 0; is_valid = false;
            is_Ecall = false; is_Ebreak = false; is_Eret = false; is_Wfi = false;
        }
        void flush() { reset(); }

        void dump() const {
            std::cout << "[EX/MWB Latch] "
                    << "Valid: " << is_valid
                    << " | RD: r" << static_cast<int>(rd)
                    << " | exResult: 0x" << std::hex << std::setw(8) << std::setfill('0') << exResult
                    << " | st_data: 0x" << std::setw(8) << st_data
                    << " | PC_Next: 0x" << std::setw(8) << pc_next
                    << std::dec
                    << " | Control [Ld:" << is_Ld << " St:" << is_St << " Wb:" << is_Wb 
                    << " Call:" << is_Call << " Clear:" << is_Clear << "]"
                    << " | Traps [Ecall:" << is_Ecall << " Ebreak:" << is_Ebreak
                    << " Eret:" << is_Eret << " Wfi:" << is_Wfi << "]\n";
        }
};

#endif