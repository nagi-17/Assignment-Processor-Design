#include <string>
#include <vector>
#include <unordered_map>
#include <stdint.h>

class PassOne {
private:
    std::unordered_map<std::string, uint32_t> symbol_table;
    std::vector<std::string> pure_instructions;
    uint32_t current_address;

public:
    PassOne() : current_address(0) {
        // def. constructor
    }

    void run(const std::vector<std::string>& clean_expanded_lines) {
        for (const std::string& line : clean_expanded_lines) {
            size_t colon_pos = line.find(':');
            size_t first_ws  = line.find_first_of(" \t");

            // A label *definition* always has its colon inside the first token
            // ("loop:"). A colon that appears after whitespace belongs to an
            // operand (e.g. "blt loop:") and must not be treated as a label.
            bool is_label_def = (colon_pos != std::string::npos) &&
                                (first_ws == std::string::npos || colon_pos < first_ws);

            if (is_label_def) {
                std::string label_name = line.substr(0, colon_pos);
                size_t last = label_name.find_last_not_of(" \t");
                if (last != std::string::npos) {
                    label_name = label_name.substr(0, last + 1);
                }

                symbol_table[label_name] = current_address;

                size_t inst_start = line.find_first_not_of(" \t", colon_pos + 1);

                if (inst_start != std::string::npos) {
                    pure_instructions.push_back(line.substr(inst_start));
                    current_address += 4;
                }
            } 
            else {
                pure_instructions.push_back(line);
                current_address += 4;
            }
        }
    }

    std::unordered_map<std::string, uint32_t> get_symbol_table() const {
        return symbol_table;
    }

    std::vector<std::string> get_pure_instructions() const {
        return pure_instructions;
    }
};