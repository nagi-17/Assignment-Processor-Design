#ifndef PIPELINE_LATCHES_HPP
#define PIPELINE_LATCHES_HPP

#include <cstdint>
#include <iostream>
#include <bitset>
#include <iomanip>
#include "control_signals.hpp"

// // Control Signals Struct (37 bits decoded from Microprogram Control Unit)
// struct ControlSignals
// {
//     std::bitset<37> bits{0}; // 37-bit Microprogrammed Control ROM bundle

//     void reset()
//     {
//         bits.reset();
//     }

//     // Direct access helper for individual bit flags in software simulation
//     bool operator[](size_t index) const
//     {
//         return bits[index];
//     }

//     typename std::bitset<37>::reference operator[](size_t index)
//     {
//         return bits[index];
//     }
// };

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
                    << " | RD: r" << static_cast<int>(rd & 0x0F) // Masked to 4 bits
                    << " | OP1: 0x" << std::hex << std::setw(8) << std::setfill('0') << op1
                    << " | OP2: 0x" << std::setw(8) << op2
                    << " | IMMX: 0x" << std::setw(8) << immx
                    << " | PC_Next: 0x" << std::setw(8) << pc_next << std::dec << "\n";
        }
};

class EX_MA1_Latch
{
    public:
        uint32_t exResult{0}; // ALU/FPU computed result or memory target address
        bool is_Ld{false};    // Memory Load control signal
        bool is_St{false};    // Memory Store control signal
        bool is_Wb{false};    // Register Writeback enable
        bool is_Clear{false}; // Security/Utility register clear signal
        uint8_t rd{0};        // Destination register address (4 bits)
        bool is_valid{false}; // Bubble flag
        uint32_t st_data{0};  // Direct op2 value for store operation

        // System/Exception control signals for MA2 Hazard Unit
        bool is_Ecall{false};
        bool is_Ebreak{false};
        bool is_Eret{false};
        bool is_Wfi{false};

        void reset()
        {
            exResult = 0;
            is_Ld = false;
            is_St = false;
            is_Wb = false;
            is_Clear = false;
            rd = 0;
            is_valid = false;
            st_data = 0;
            is_Ecall = false;
            is_Ebreak = false;
            is_Eret = false;
            is_Wfi = false;
        }

        void flush()
        {
            reset();
        }

        void dump() const
        {
            std::cout << "[EX/MA1 Latch] "
                    << "Valid: " << is_valid
                    << " | RD: r" << static_cast<int>(rd & 0x0F)
                    << " | exResult: 0x" << std::hex << std::setw(8) << std::setfill('0') << exResult
                    << " | st_data: 0x" << std::setw(8) << st_data
                    << std::dec
                    << " | Ld:" << is_Ld << " St:" << is_St << " Wb:" << is_Wb << " Clear:" << is_Clear
                    << " | Traps [Ecall:" << is_Ecall << " Ebreak:" << is_Ebreak
                    << " Eret:" << is_Eret << " Wfi:" << is_Wfi << "]\n";
        }
};

class MA1_MA2_Latch
{
    public:
        uint32_t exResult{0}; // ALU/FPU result or memory address
        bool is_St{false};    // Memory Store control signal
        bool is_Ld{false};    // Memory Load control signal
        bool is_Wb{false};    // Register Writeback enable
        bool is_Clear{false}; // Register clear signal
        uint8_t rd{0};        // Destination register address (4 bits)
        bool is_valid{false}; // Bubble flag
        uint32_t st_data{0};  // Direct op2 data value for store

        // System/Exception signals
        bool is_Ecall{false};
        bool is_Ebreak{false};
        bool is_Eret{false};
        bool is_Wfi{false};

        // Stack fault flag set by MA1 boundary verification
        bool stack_fault{false};

        void reset()
        {
            exResult = 0;
            is_St = false;
            is_Ld = false;
            is_Wb = false;
            is_Clear = false;
            rd = 0;
            is_valid = false;
            st_data = 0;
            is_Ecall = false;
            is_Ebreak = false;
            is_Eret = false;
            is_Wfi = false;
            stack_fault = false;
        }

        void flush()
        {
            reset();
        }

        void dump() const
        {
            std::cout << "[MA1/MA2 Latch] "
                    << "Valid: " << is_valid
                    << " | RD: r" << static_cast<int>(rd & 0x0F)
                    << " | exResult: 0x" << std::hex << std::setw(8) << std::setfill('0') << exResult
                    << " | st_data: 0x" << std::setw(8) << st_data
                    << std::dec
                    << " | Ld:" << is_Ld << " St:" << is_St << " Wb:" << is_Wb
                    << " | StackFault:" << stack_fault
                    << " | Traps [Ecall:" << is_Ecall << " Ebreak:" << is_Ebreak
                    << " Eret:" << is_Eret << " Wfi:" << is_Wfi << "]\n";
        }
};

class MA2_WB_Latch
{
    public:
        uint32_t exResult{0};  // Computed ALU/FPU result or link address
        bool is_Ld{false};     // Control signal for MUX selection in WB
        bool is_Wb{false};     // Register Writeback enable
        bool is_Clear{false};  // Register clear signal
        uint8_t rd{0};         // Destination register address (4 bits)
        bool is_valid{false};  // Validity flag for commit/debug checking
        uint32_t read_data{0}; // Data memory read result

        void reset()
        {
            exResult = 0;
            is_Ld = false;
            is_Wb = false;
            is_Clear = false;
            rd = 0;
            is_valid = false;
            read_data = 0;
        }

        void flush()
        {
            reset();
        }

        void dump() const
        {
            std::cout << "[MA2/WB Latch] "
                    << "Valid: " << is_valid
                    << " | RD: r" << static_cast<int>(rd & 0x0F)
                    << " | exResult: 0x" << std::hex << std::setw(8) << std::setfill('0') << exResult
                    << " | read_data: 0x" << std::setw(8) << read_data
                    << std::dec
                    << " | Ld:" << is_Ld << " Wb:" << is_Wb << " Clear:" << is_Clear << "\n";
        }
};

#endif