#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

class Preprocessor {
private:
    std::string sanitize_line(const std::string& raw_line) {
        std::string line = raw_line;

        // remove comments
        size_t comment_pos = line.find_first_of(";#");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        // remove whitespace (spaces/tabs/carriage returns)
        const std::string whitespace = " \t\r";
        size_t first = line.find_first_not_of(whitespace);

        // empty string
        if (first == std::string::npos) {
            return "";
        }
        
        size_t last = line.find_last_not_of(whitespace);
        return line.substr(first, (last - first + 1));
    }

    std::string extract_first_word(const std::string& line) {
        size_t space_pos = line.find_first_of(" \t");
        if (space_pos == std::string::npos) {
            return line; // no spaces like ebreak
        }
        return line.substr(0, space_pos);
    }

    // Helper to extract two operands whether separated by comma or whitespace
    bool parse_two_operands(const std::string& line, std::string& op1, std::string& op2) {
        size_t space_pos = line.find_first_of(" \t");
        if (space_pos == std::string::npos) return false;
        std::string operands = line.substr(space_pos + 1);

        size_t start = operands.find_first_not_of(" \t");
        if (start == std::string::npos) return false;
        operands = operands.substr(start);

        size_t comma_pos = operands.find(',');
        if (comma_pos != std::string::npos) {
            op1 = operands.substr(0, comma_pos);
            size_t second_start = operands.find_first_not_of(" \t", comma_pos + 1);
            if (second_start == std::string::npos) return false;
            op2 = operands.substr(second_start);
        } else {
            size_t sep = operands.find_first_of(" \t");
            if (sep == std::string::npos) return false;
            op1 = operands.substr(0, sep);
            size_t second_start = operands.find_first_not_of(" \t", sep + 1);
            if (second_start == std::string::npos) return false;
            op2 = operands.substr(second_start);
        }

        size_t last1 = op1.find_last_not_of(" \t\r");
        if (last1 != std::string::npos) op1 = op1.substr(0, last1 + 1);
        size_t last2 = op2.find_last_not_of(" \t\r");
        if (last2 != std::string::npos) op2 = op2.substr(0, last2 + 1);

        return !op1.empty() && !op2.empty();
    }

    bool is_number_str(const std::string& s) {
        if (s.empty()) return false;
        size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (start == 1 && s.size() == 1) return false;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            for (size_t i = 2; i < s.size(); ++i) {
                if (!isxdigit(s[i])) return false;
            }
            return true;
        }
        for (size_t i = start; i < s.size(); ++i) {
            if (!isdigit(s[i])) return false;
        }
        return true;
    }

    // implementing mov using add instruction
    // mov rd, rs       -> add rd, rs, r12   (register copy)
    // mov rd, imm      -> add rd, r12, imm  (load immediate; r12 is hardwired zero)
    void handle_mov(const std::string& line, std::vector<std::string>& output) {
        std::string rd, src;
        if (!parse_two_operands(line, rd, src)) {
            output.push_back(line);
            return;
        }

        if (is_number_str(src)) {
            output.push_back("add " + rd + ", r12, " + src);
        } else {
            output.push_back("add " + rd + ", " + src + ", r12");
        }
    }

    // implementing fmov using movu and movh to load 32-bit IEEE-754 float
    void handle_fmov(const std::string& line, std::vector<std::string>& output) {
        std::string rd, float_str;
        if (!parse_two_operands(line, rd, float_str)) {
            output.push_back(line);
            return;
        }

        try {
            float f_val = std::stof(float_str);
            union {
                float f;
                uint32_t i;
            } ieee;
            ieee.f = f_val;

            uint16_t upper_half = (ieee.i >> 16) & 0xFFFF;
            uint16_t lower_half = ieee.i & 0xFFFF;

            output.push_back("movu " + rd + ", " + std::to_string(upper_half));
            output.push_back("movh " + rd + ", " + std::to_string(lower_half));
        } catch (...) {
            output.push_back(line);
        }
    }

    void handle_push(const std::string& line, std::vector<std::string>& output) {
        size_t space_pos = line.find_first_of(" \t");
        size_t reg_start = line.find_first_not_of(" \t", space_pos);
        std::string reg = line.substr(reg_start);
        
        output.push_back("sub r14, r14, 4");
        output.push_back("st " + reg + ", r14, 0");
    }

    void handle_pop(const std::string& line, std::vector<std::string>& output) {
        size_t space_pos = line.find_first_of(" \t");
        size_t reg_start = line.find_first_not_of(" \t", space_pos);
        std::string reg = line.substr(reg_start);
        
        output.push_back("ld " + reg + ", r14, 0");
        output.push_back("add r14, r14, 4");
    }

public:
    std::vector<std::string> process(const std::vector<std::string>& raw_lines) {
        std::vector<std::string> clean_expanded_lines;

        for (const std::string& raw_line : raw_lines) {
            std::string clean_line = sanitize_line(raw_line);

            if (clean_line.empty()) {
                continue;
            }

            std::string first_word = extract_first_word(clean_line);

            if (first_word == "mov") {
                handle_mov(clean_line, clean_expanded_lines);
            }
            else if (first_word == "fmov") {
                handle_fmov(clean_line, clean_expanded_lines);
            }
            else if (first_word == "push") {
                handle_push(clean_line, clean_expanded_lines);
            } 
            else if (first_word == "pop") {
                handle_pop(clean_line, clean_expanded_lines);
            } 
            else {
                clean_expanded_lines.push_back(clean_line);
            }
        }
        return clean_expanded_lines;
    }
};