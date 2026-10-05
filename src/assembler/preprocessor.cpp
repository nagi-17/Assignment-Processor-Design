#include <iostream>
#include <vector>
#include <string>

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

    // implementing mov using add instruction
    void handle_mov(const std::string& line, std::vector<std::string>& output) {
        size_t space_pos = line.find_first_of(" \t");
        std::string operands = line.substr(space_pos + 1);
        
        size_t comma_pos = operands.find(',');
        if (comma_pos != std::string::npos) {
            std::string rd = operands.substr(0, comma_pos);
            
            size_t rs_start = operands.find_first_not_of(" \t", comma_pos + 1);
            std::string rs = operands.substr(rs_start);
            
            output.push_back("add " + rd + ", " + rs + ", r12");
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