// register_file.hpp
// 16-register file for RISC201.  r12 is hardwired to 0.

#ifndef REGISTER_FILE_HPP
#define REGISTER_FILE_HPP

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>

class RegisterFile {
public:
    static constexpr uint8_t REG_ZERO = 12; // r12 hardwired to 0

    RegisterFile() { regs_.fill(0); }

    // Read a register.  r12 always returns 0.
    uint32_t read(uint8_t reg) const {
        reg &= 0xF;
        if (reg == REG_ZERO) return 0;
        return regs_[reg];
    }

    // Write a register.  Writes to r12 are silently ignored.
    void write(uint8_t reg, uint32_t value) {
        reg &= 0xF;
        if (reg == REG_ZERO) return;
        regs_[reg] = value;
    }

    // Raw access to the full array (read-only).  Used by the pipeline decode
    // stage to fetch both operands in one shot.
    const std::array<uint32_t, 16>& getAll() const { return regs_; }

    // Reset all registers to 0.
    void reset() { regs_.fill(0); }

    // Dump all registers to stdout.
    void dump() const {
        for (int i = 0; i < 16; ++i) {
            std::cout << "r" << std::setw(2) << std::setfill(' ') << i << " : 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << regs_[i]
                      << std::dec;
            if (i % 4 == 3) std::cout << '\n';
            else            std::cout << "  ";
        }
    }

private:
    std::array<uint32_t, 16> regs_{};
};

#endif // REGISTER_FILE_HPP
