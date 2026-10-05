#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include "preprocessor.cpp"
#include "symbol_table.cpp"
#include "bin_generator.cpp"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./assembler <input_file.s> <output_file.bin>\n";
        return 1;
    }

    std::vector<std::string> raw_lines;
    FILE* input_file = fopen(argv[1], "r");
    if (input_file == NULL) {
        std::cerr << "Error: Could not open input file\n";
        return 1;
    }
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), input_file)) {
        std::string line = buffer;
        
        if (!line.empty() && line[line.length()-1] == '\n') {
            line.pop_back();
        }
        raw_lines.push_back(line);
    }
    fclose(input_file);

    Preprocessor preprocessor;
    std::vector<std::string> clean_lines = preprocessor.process(raw_lines);

    PassOne pass_one;
    pass_one.run(clean_lines);
    auto pure_instructions = pass_one.get_pure_instructions();
    auto symbol_table = pass_one.get_symbol_table();

    BinaryGenerator pass_two;
    std::vector<std::string> machine_code = pass_two.generate(pure_instructions, symbol_table);

    FILE* output_file = fopen(argv[2], "w");
    
    if (output_file == NULL) {
        std::cerr << "Error: Could not create output file\n";
        return 1;
    }

    for (int i = 0; i < machine_code.size(); i++) {
        fprintf(output_file, "%s\n", machine_code[i].c_str());
    }
    fclose(output_file);

    std::cout << "Machine code generated successfully. " << machine_code.size() << " instructions compiled.\n";
    return 0;
}