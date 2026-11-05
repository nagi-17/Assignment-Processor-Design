#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <stdint.h>
#include "../headers/isa_config.hpp" 

class BinaryGenerator {
private:
    std::string integer_to_16bit_binary(std::string imm_str) {
        int val = std::stoi(imm_str);
        std::string binary = "";
        
        for (int i = 15; i >= 0; i--) {
            if ((val >> i) & 1) {
                binary += "1";
            } else {
                binary += "0";
            }
        }
        return binary;
    }

    // Two's-complement binary string of `bits` bits (MSB first).
    std::string signed_to_binary(int val, int bits) {
        std::string binary = "";
        for (int i = bits - 1; i >= 0; i--) {
            binary += ((val >> i) & 1) ? "1" : "0";
        }
        return binary;
    }

    // Resolve a branch/call operand into a PC-relative BYTE offset.
    // The operand may be a literal offset ("-16"), a label ("loop") or a
    // label reference carrying a trailing colon ("loop:").
    int resolve_branch_offset(const std::string& operand, uint32_t current_address,
                              const std::unordered_map<std::string, uint32_t>& symbol_table) {
        std::string symbol = operand;
        if (!symbol.empty() && symbol.back() == ':') {
            symbol.pop_back();
        }

        if (is_numeric(symbol)) {
            return std::stoi(symbol);
        }

        auto it = symbol_table.find(symbol);
        if (it == symbol_table.end()) {
            throw std::runtime_error("undefined label '" + symbol + "'");
        }
        return static_cast<int>(it->second) - static_cast<int>(current_address);
    }

    std::vector<std::string> split_operands(std::string str) {
        std::vector<std::string> operands;
        std::string current_op = "";
        
        for (int i = 0; i < str.length(); i++) {
            if (str[i] == ' ' || str[i] == '\t') {
                continue;
            }
            if (str[i] == ',') {
                if (current_op != "") {
                    operands.push_back(current_op);
                    current_op = "";
                }
            } else {
                current_op += str[i];
            }
        }
        
        if (current_op != "") {
            operands.push_back(current_op);
        }
        
        return operands;
    }

    bool is_numeric(std::string str) {
        if (str.length() == 0) {
            return false;
        }
        int start = 0;
        if (str[0] == '-') {
            start = 1;
        }
        
        for (int i = start; i < str.length(); i++) {
            if (str[i] < '0' || str[i] > '9') {
                return false;
            }
        }
        return true;
    }

public:
    std::vector<std::string> generate(const std::vector<std::string>& pure_instructions, const std::unordered_map<std::string, uint32_t>& symbol_table) {
        std::vector<std::string> mach_code;
        uint32_t current_address = 0;

        for (int i = 0; i < pure_instructions.size(); i++) {
            std::string line = pure_instructions[i];
            
            int space_idx = -1;
            for (int j = 0; j < line.length(); j++) {
                if (line[j] == ' ' || line[j] == '\t') {
                    space_idx = j;
                    break;
                }
            }

            std::string mnemonic = "";
            std::string operands_str = "";

            if (space_idx == -1) {
                mnemonic = line;
            } else {
                mnemonic = line.substr(0, space_idx);
                operands_str = line.substr(space_idx + 1);
            }
            
            std::vector<std::string> ops = split_operands(operands_str);
            std::string opcode = OPCODE_MAP.at(mnemonic);
            InstFormat format;

            if (INST_FORMAT_MAP.find(mnemonic) != INST_FORMAT_MAP.end()) {
                format = INST_FORMAT_MAP.at(mnemonic);
            } else {
                bool is_imm = (ops.size() > 0 && is_numeric(ops[ops.size()-1]));
                
                if (ops.size() == 3) {
                    format = is_imm ? InstFormat::I_TYPE_3 : InstFormat::R_TYPE_3;
                } 
                else if (ops.size() == 2) {
                    format = is_imm ? InstFormat::I_TYPE_2 : InstFormat::R_TYPE_2;
                }
            }

            std::string final_32bit = opcode;

            switch (format) {
                case InstFormat::R_TYPE_3:
                    final_32bit += "0"; 
                    final_32bit += REG_MAP.at(ops[0]); 
                    final_32bit += REG_MAP.at(ops[1]); 
                    final_32bit += REG_MAP.at(ops[2]); 
                    final_32bit += "0000000000000";         
                    break;

                case InstFormat::R_TYPE_2:
                    final_32bit += "0";
                    final_32bit += "0000";                  
                    final_32bit += REG_MAP.at(ops[0]); 
                    final_32bit += REG_MAP.at(ops[1]); 
                    final_32bit += "0000000000000";         
                    break;

                case InstFormat::R_TYPE_1:
                    final_32bit += "0";
                    final_32bit += REG_MAP.at(ops[0]); 
                    final_32bit += "0000";                  
                    final_32bit += "0000";                  
                    final_32bit += "0000000000000";
                    break;

                case InstFormat::I_TYPE_3:
                    final_32bit += "1";
                    final_32bit += REG_MAP.at(ops[0]); 
                    final_32bit += REG_MAP.at(ops[1]); 
                    final_32bit += integer_to_16bit_binary(ops[2]);
                    final_32bit += "0";                     
                    break;

                case InstFormat::I_TYPE_2:
                    final_32bit += "1";
                    final_32bit += REG_MAP.at(ops[0]); 
                    final_32bit += "0000";                  
                    final_32bit += integer_to_16bit_binary(ops[1]);
                    final_32bit += "0";
                    break;

                case InstFormat::I_TYPE_CSRW:
                    final_32bit += "1";
                    final_32bit += "0000";                  
                    final_32bit += REG_MAP.at(ops[1]); 
                    final_32bit += integer_to_16bit_binary(ops[0]); 
                    final_32bit += "0";
                    break;

                case InstFormat::I_TYPE_1: {
                    // Branches and calls store a 25-bit PC-relative BYTE offset
                    // in bits [24:0]. The decoder sign-extends 25 bits and then
                    // divides by 4 to convert into instruction-index units.
                    // The target is always the final operand, so both "blt label"
                    // and "beq rs1, rs2, label" encode the same way.
                    if (ops.empty()) {
                        throw std::runtime_error(
                            "branch/call instruction '" + mnemonic + "' has no target operand");
                    }
                    const int offset =
                        resolve_branch_offset(ops.back(), current_address, symbol_table);
                    final_32bit += "1";
                    final_32bit += signed_to_binary(offset, 25);
                    break;
                }

                case InstFormat::SYS_TYPE:
                    final_32bit += "00000000000000000000000000"; 
                    break;

                case InstFormat::F_TYPE:
                    final_32bit += "1";
                    final_32bit += REG_MAP.at(ops[0]);
                    {
                        uint32_t imm21 = 0;
                        try {
                            imm21 = static_cast<uint32_t>(std::stoul(ops[1], nullptr, 0)) & 0x1FFFFFu;
                        } catch (...) {
                            imm21 = 0;
                        }
                        final_32bit += std::bitset<21>(imm21).to_string();
                    }
                    break;
            }

            mach_code.push_back(final_32bit);
            current_address += 4;
        }

        return mach_code;
    }
};