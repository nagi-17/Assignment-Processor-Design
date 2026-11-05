// writeback_common.hpp
// The write back step is the same for the 4 stage and the 6 stage design,
// so it is written once here and both of them call it.
//
// NOTE: the register file comes from another file. Change the include and the class name
// below if yours is different. It only needs a function:  void write(int reg, uint32_t value)

#ifndef WRITEBACK_COMMON_HPP
#define WRITEBACK_COMMON_HPP

#include <cstdint>
#include "register_file.hpp"

inline void writeBackToRegFile(RegisterFile &rf, bool valid, bool is_Wb, bool is_Clear, bool is_Ld,
                               uint8_t rd, uint32_t exResult, uint32_t read_data)
{
    if (!valid)       // bubble, nothing to do
        return;

    if (is_Clear)     // clear signal: make the register 0
    {
        rf.write(rd, 0);
        return;
    }

    if (!is_Wb)       // this instruction does not write a register
        return;

    // mux: for a load we write the data we read from memory, otherwise the alu/fpu result
    if (is_Ld)
        rf.write(rd, read_data);
    else
        rf.write(rd, exResult);
}

#endif
