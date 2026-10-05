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

            if (colon_pos != std::string::npos) {
                std::string label_name = line.substr(0, colon_pos);

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
};