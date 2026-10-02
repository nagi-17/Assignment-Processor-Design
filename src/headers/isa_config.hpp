#ifndef ISA_CONFIG_HPP
#define ISA_CONFIG_HPP

#include <string>
#include <unordered_map>

enum class InstFormat {
    R_TYPE,
    I_TYPE,
    F_TYPE,
    SYS_TYPE // for zero-operand inst.
    /* so basically whenever the assembler encounters any inst. with inst. format as SYS_TYPE,
    it will just fecth for the opcode for the inst. and set all other inst. bits to 0 */
};

const std::unordered_map<std::string, std::string> OPCODE_MAP = {
    {"nop",   "000000"},
    {"add",   "000001"},
    {"sub",   "000010"},
    {"mul",   "000011"},
    {"div",   "000100"},
    {"mod",   "000101"},
    {"cmp",   "000110"},
    {"and",   "001000"},
    {"or",    "001001"},
    {"xor",   "001010"},
    {"not",   "001011"},
    {"lsl",   "001100"},
    {"lsr",   "001101"},
    {"asr",   "001110"},

    {"fadd",  "010001"},
    {"fsub",  "010010"},
    {"fmul",  "010011"},
    {"fdiv",  "010100"},
    {"fcmp",  "010110"},
    {"fmov",  "011000"},

    {"ld",    "100000"},
    {"st",    "100001"},
    {"movu",  "100010"},
    {"movh",  "100011"},
    {"csrr",  "101000"},
    {"csrw",  "101001"},
    {"chk",   "101100"},
    {"clrz",  "101101"},

    {"b",     "110000"},
    {"beq",   "110001"},
    {"bne",   "110010"},
    {"bgt",   "110011"},
    {"blt",   "110100"},
    {"call",  "111000"},
    {"ret",   "111001"},
    {"ecall", "111100"},
    {"eret",  "111101"},
    {"ebreak","111110"},
    {"wfi",   "111111"}
};

const std::unordered_map<std::string, std::string> REG_MAP = {
    {"r0",  "0000"}, {"r1",  "0001"}, {"r2",  "0010"}, {"r3",  "0011"},
    {"r4",  "0100"}, {"r5",  "0101"}, {"r6",  "0110"}, {"r7",  "0111"},
    {"r8",  "1000"}, {"r9",  "1001"}, {"r10", "1010"}, {"r11", "1011"},
    {"r12", "1100"}, {"r13", "1101"}, {"r14", "1110"}, {"r15", "1111"},
    
    // alt. name for r12-r15
    {"zero","1100"},
    {"fp",  "1101"},
    {"sp",  "1110"},
    {"ra",  "1111"}
};

const std::unordered_map<std::string, InstFormat> INST_FORMAT_MAP = {
    {"fadd", InstFormat::R_TYPE},
    {"fsub", InstFormat::R_TYPE},
    {"fmul", InstFormat::R_TYPE},
    {"fdiv", InstFormat::R_TYPE},
    {"fcmp", InstFormat::R_TYPE},

    {"fmov", InstFormat::F_TYPE},

    {"ld", InstFormat::R_TYPE},
    {"st", InstFormat::R_TYPE},

    {"nop",    InstFormat::SYS_TYPE},
    {"ret",    InstFormat::SYS_TYPE},
    {"ecall",  InstFormat::SYS_TYPE},
    {"eret",   InstFormat::SYS_TYPE},
    {"ebreak", InstFormat::SYS_TYPE},
    {"wfi",    InstFormat::SYS_TYPE}
};
// Inst. which allow multiple inst. format are not included in FORMAT_MAP, as they will be handled dynamically

#endif
