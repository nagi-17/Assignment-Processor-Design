#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
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

                case InstFormat::I_TYPE_1: 
                    final_32bit += "1";
                    final_32bit += "0000";                  
                    
                    if (mnemonic == "b" || mnemonic == "call") {
                        final_32bit += "0000"; 
                        int offset = symbol_table.at(ops[0]) - current_address;
                        final_32bit += integer_to_16bit_binary(std::to_string(offset));
                    } else {
                        final_32bit += REG_MAP.at(ops[0]); 
                        int offset = symbol_table.at(ops[2]) - current_address;
                        final_32bit += integer_to_16bit_binary(std::to_string(offset));
                    }
                    final_32bit += "0";
                    break;

                case InstFormat::SYS_TYPE:
                    final_32bit += "00000000000000000000000000"; 
                    break;
            }

            mach_code.push_back(final_32bit);
            current_address += 4;
        }

        return mach_code;
    }
};