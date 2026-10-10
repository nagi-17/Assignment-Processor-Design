#include <iostream>
#include <string>
#include <unordered_map>
#include <bitset>
#include <vector>
#include <iomanip>

// Enum mapping each control signal to a bit index in the microprogram memory
enum class Signal {
    // Basic Control, Branch & Memory (0 - 10)
    IS_ST = 0, IS_LD, IS_BEQ, IS_BGT, IS_BLT, IS_BNE, IS_RET, IS_IMMEDIATE, IS_WB, IS_UBRANCH, IS_CALL,

    // Integer ALU Signals (11 - 22)
    // IS_MOV control signal is not required, as it is implemented using preprocessing.
    IS_ADD, IS_SUB, IS_CMP, IS_MUL, IS_DIV, IS_MOD, IS_LSL, IS_LSR, IS_ASR, IS_OR, IS_AND, IS_NOT,

    // Extended System & Special Signals (23 - 29)
    IS_WFI, IS_STALL, IS_ERET, IS_CSRR, IS_CSRW, IS_SYSCALL, IS_EBREAK,

    // Floating-Point Unit (FPU) Signals (30 - 35)
    IS_FLOAT, IS_FADD, IS_FSUB, IS_FMUL, IS_FDIV, IS_FCMP,

    // Secure/Utility Signal (36)
    IS_CLEAR,

    // Total Count Sentinel
    COUNT
};

constexpr size_t CONTROL_WORD_WIDTH = static_cast<size_t>(Signal::COUNT);
using ControlWord = std::bitset<CONTROL_WORD_WIDTH>;

// Helper to set individual bits cleanly
inline void enableSignal(ControlWord& cw, Signal sig) {
    cw.set(static_cast<size_t>(sig));
}

class MicroprogrammedControlUnit {
private:
    // Stored Microprogram Memory (Control Store ROM/RAM)
    std::unordered_map<std::string, ControlWord> controlStore;

    void initializeControlStore() {

        // 1. INTEGER ALU INSTRUCTIONS (Opcode 00xxxx)
        // mov instruction is implemented using preprocessing, and hence it is already taken care of, no need of including it here.

        // add - R-format (000000)
        ControlWord addCw;
        enableSignal(addCw, Signal::IS_ADD);
        enableSignal(addCw, Signal::IS_WB);
        controlStore["0000000"] = addCw;
        // add - I-format (000000)
        ControlWord addICw;
        enableSignal(addICw, Signal::IS_ADD);
        enableSignal(addICw, Signal::IS_WB);
        enableSignal(addICw, Signal::IS_IMMEDIATE);
        controlStore["0000001"] = addICw;

        // sub - R-format (000001)
        ControlWord subCw;
        enableSignal(subCw, Signal::IS_SUB);
        enableSignal(subCw, Signal::IS_WB);
        controlStore["0000010"] = subCw;
        // sub - I-format (000001)
        ControlWord subICw;
        enableSignal(subICw, Signal::IS_SUB);
        enableSignal(subICw, Signal::IS_WB);
        enableSignal(subICw, Signal::IS_IMMEDIATE);
        controlStore["0000011"] = subICw;
        
        // cmp - R-format (000010)
        ControlWord cmpCw;
        enableSignal(cmpCw, Signal::IS_CMP);
        controlStore["0000100"] = cmpCw;
        // cmp - I-format (000010)
        ControlWord cmpICw;
        enableSignal(cmpICw, Signal::IS_CMP);
        enableSignal(cmpICw, Signal::IS_IMMEDIATE);
        controlStore["0000101"] = cmpICw;

        // mul - R-format (000011)
        ControlWord mulCw;
        enableSignal(mulCw, Signal::IS_MUL);
        enableSignal(mulCw, Signal::IS_WB);
        controlStore["0000110"] = mulCw;
        // mul - I-format (000011)
        ControlWord mulICw;
        enableSignal(mulICw, Signal::IS_MUL);
        enableSignal(mulICw, Signal::IS_WB);
        enableSignal(mulICw, Signal::IS_IMMEDIATE);
        controlStore["0000111"] = mulICw;

        // div - R-format (000100)
        ControlWord divCw;
        enableSignal(divCw, Signal::IS_DIV);
        enableSignal(divCw, Signal::IS_WB);
        controlStore["0001000"] = divCw;
        // div - I-format (000100)
        ControlWord divICw;
        enableSignal(divICw, Signal::IS_DIV);
        enableSignal(divICw, Signal::IS_WB);
        enableSignal(divICw, Signal::IS_IMMEDIATE);
        controlStore["0001001"] = divICw;
        
        // mod - R-format (000101)
        ControlWord modCw;
        enableSignal(modCw, Signal::IS_MOD);
        enableSignal(modCw, Signal::IS_WB);
        controlStore["0001010"] = modCw;
        // mod - I-format (000101)
        ControlWord modICw;
        enableSignal(modCw, Signal::IS_MOD);
        enableSignal(modCw, Signal::IS_WB);
        controlStore["0001011"] = modCw;

        // lsl - R-format (000110)
        ControlWord lslCw;
        enableSignal(lslCw, Signal::IS_LSL);
        enableSignal(lslCw, Signal::IS_WB);
        controlStore["0001100"] = lslCw;
        // lsl - I-format (000110)
        ControlWord lslICw;
        enableSignal(lslICw, Signal::IS_LSL);
        enableSignal(lslICw, Signal::IS_WB);
        controlStore["0001101"] = lslICw;

        // lsr - R-format (000111)
        ControlWord lsrCw;
        enableSignal(lsrCw, Signal::IS_LSR);
        enableSignal(lsrCw, Signal::IS_WB);
        controlStore["0001110"] = lsrCw;
        // lsr - I-format (000111)
        ControlWord lsrICw;
        enableSignal(lsrICw, Signal::IS_LSR);
        enableSignal(lsrICw, Signal::IS_WB);
        enableSignal(lsrICw, Signal::IS_IMMEDIATE);
        controlStore["0001111"] = lsrICw;

        // asr - R-format (001000)
        ControlWord asrCw;
        enableSignal(asrCw, Signal::IS_ASR);
        enableSignal(asrCw, Signal::IS_WB);
        controlStore["0010000"] = asrCw;
        // asr - I-format (001000)
        ControlWord asrICw;
        enableSignal(asrICw, Signal::IS_ASR);
        enableSignal(asrICw, Signal::IS_WB);
        enableSignal(asrICw, Signal::IS_IMMEDIATE);
        controlStore["0010001"] = asrICw;

        // or - R-format (001001)
        ControlWord orCw;
        enableSignal(orCw, Signal::IS_OR);
        enableSignal(orCw, Signal::IS_WB);
        controlStore["0010010"] = orCw;
        // or - I-format (001001)
        ControlWord orICw;
        enableSignal(orICw, Signal::IS_OR);
        enableSignal(orICw, Signal::IS_WB);
        enableSignal(orICw, Signal::IS_IMMEDIATE);
        controlStore["0010011"] = orICw;

        // and - R-format (001010)
        ControlWord andCw;
        enableSignal(andCw, Signal::IS_AND);
        enableSignal(andCw, Signal::IS_WB);
        controlStore["0010100"] = andCw;
        // and - I-format (001010)
        ControlWord andICw;
        enableSignal(andICw, Signal::IS_AND);
        enableSignal(andICw, Signal::IS_WB);
        enableSignal(andICw, Signal::IS_IMMEDIATE);
        controlStore["0010101"] = andICw;

        // not - R-format (001011)
        ControlWord notCw;
        enableSignal(notCw, Signal::IS_NOT);
        enableSignal(notCw, Signal::IS_WB);
        controlStore["0010110"] = notCw;
        // not - I-format (001011)
        ControlWord notICw;
        enableSignal(notICw, Signal::IS_NOT);
        enableSignal(notICw, Signal::IS_WB);
        enableSignal(notICw, Signal::IS_IMMEDIATE);
        controlStore["0010111"] = notICw;

        // 2. FPU INSTRUCTIONS (Opcode 01xxxx)
        
        // fadd - R-format (010001)
        ControlWord faddCw;
        enableSignal(faddCw, Signal::IS_FLOAT);
        enableSignal(faddCw, Signal::IS_FADD);
        enableSignal(faddCw, Signal::IS_WB);
        controlStore["0100010"] = faddCw;

        // fsub - R-format (010010)
        ControlWord fsubCw;
        enableSignal(fsubCw, Signal::IS_FLOAT);
        enableSignal(fsubCw, Signal::IS_FSUB);
        enableSignal(fsubCw, Signal::IS_WB);
        controlStore["0100100"] = fsubCw;

        // fmul - R-format (010011)
        ControlWord fmulCw;
        enableSignal(fmulCw, Signal::IS_FLOAT);
        enableSignal(fmulCw, Signal::IS_FMUL);
        enableSignal(fmulCw, Signal::IS_WB);
        controlStore["0100110"] = fmulCw;

        // fdiv - R-format (010100)
        ControlWord fdivCw;
        enableSignal(fdivCw, Signal::IS_FLOAT);
        enableSignal(fdivCw, Signal::IS_FDIV);
        enableSignal(fdivCw, Signal::IS_WB);
        controlStore["0101000"] = fdivCw;

        // fcmp - R-format (010110)
        ControlWord fcmpCw;
        enableSignal(fcmpCw, Signal::IS_FLOAT);
        enableSignal(fcmpCw, Signal::IS_FCMP);
        controlStore["0101100"] = fcmpCw;

        // 3. MEMORY & SYSTEM INSTRUCTIONS (Opcode 10xxxx)
        
        // ld - I-format (100000)
        ControlWord ldCw;
        enableSignal(ldCw, Signal::IS_LD);
        enableSignal(ldCw, Signal::IS_IMMEDIATE);
        enableSignal(ldCw, Signal::IS_WB);
        enableSignal(ldCw, Signal::IS_ADD); // Address generation logic
        controlStore["1000001"] = ldCw;

        // st - I-format (100001)
        ControlWord stCw;
        enableSignal(stCw, Signal::IS_ST);
        enableSignal(stCw, Signal::IS_IMMEDIATE);
        enableSignal(stCw, Signal::IS_ADD); // Address generation logic
        controlStore["1000011"] = stCw;

        // movu - I-format (100010)
        ControlWord movuCw;
        enableSignal(movuCw, Signal::IS_IMMEDIATE);
        enableSignal(movuCw, Signal::IS_WB);
        controlStore["1000101"] = movuCw;

        // movh - I-format (100011)
        ControlWord movhCw;
        enableSignal(movhCw, Signal::IS_IMMEDIATE);
        enableSignal(movhCw, Signal::IS_WB);
        controlStore["1000111"] = movhCw;

        // csrr - I-format (101000)
        ControlWord csrrCw;
        enableSignal(csrrCw, Signal::IS_CSRR);
        enableSignal(csrrCw, Signal::IS_IMMEDIATE);
        enableSignal(csrrCw, Signal::IS_WB);
        controlStore["1010001"] = csrrCw;

        // csrw - I-format (101001)
        ControlWord csrwCw;
        enableSignal(csrwCw, Signal::IS_CSRW);
        enableSignal(csrwCw, Signal::IS_IMMEDIATE);
        controlStore["1010011"] = csrwCw;

        // chk - R-format (101100)
        ControlWord chkCw;
        enableSignal(chkCw, Signal::IS_CMP);
        controlStore["1011000"] = chkCw;

        // clrz - R-format (101101)
        ControlWord clrzCw;
        enableSignal(clrzCw, Signal::IS_CLEAR);
        enableSignal(clrzCw, Signal::IS_WB);
        controlStore["1011010"] = clrzCw;


        // 4. CONTROL FLOW INSTRUCTIONS (Opcode 11xxxx)
        
        // b - I-format (110000)
        ControlWord bCw;
        enableSignal(bCw, Signal::IS_UBRANCH);
        enableSignal(bCw, Signal::IS_IMMEDIATE);
        controlStore["1100001"] = bCw;

        // beq - I-format (110001)
        ControlWord beqCw;
        enableSignal(beqCw, Signal::IS_BEQ);
        enableSignal(beqCw, Signal::IS_IMMEDIATE);
        controlStore["1100011"] = beqCw;

        // bne - I-format (110010)
        ControlWord bneCw;
        enableSignal(bneCw, Signal::IS_BNE);
        enableSignal(bneCw, Signal::IS_IMMEDIATE);
        controlStore["1100101"] = bneCw;

        // bgt - I-format (110011)
        ControlWord bgtCw;
        enableSignal(bgtCw, Signal::IS_BGT);
        enableSignal(bgtCw, Signal::IS_IMMEDIATE);
        controlStore["1100111"] = bgtCw;

        // blt - I-format (110100)
        ControlWord bltCw;
        enableSignal(bltCw, Signal::IS_BLT);
        enableSignal(bltCw, Signal::IS_IMMEDIATE);
        controlStore["1101001"] = bltCw;

        // call - I-format (111000)
        ControlWord callCw;
        enableSignal(callCw, Signal::IS_CALL);
        enableSignal(callCw, Signal::IS_UBRANCH);
        enableSignal(callCw, Signal::IS_WB);
        enableSignal(callCw, Signal::IS_IMMEDIATE);
        controlStore["1110001"] = callCw;

        // ret - R-format (111001)
        ControlWord retCw;
        enableSignal(retCw, Signal::IS_RET);
        enableSignal(retCw, Signal::IS_UBRANCH);
        controlStore["1110010"] = retCw;

        // ecall - R-format (111100)
        ControlWord ecallCw;
        enableSignal(ecallCw, Signal::IS_SYSCALL);
        controlStore["1111000"] = ecallCw;

        // eret - R-format (111101)
        ControlWord eretCw;
        enableSignal(eretCw, Signal::IS_ERET);
        controlStore["1111010"] = eretCw;

        // ebreak - R-format (111110)
        ControlWord ebreakCw;
        enableSignal(ebreakCw, Signal::IS_EBREAK);
        controlStore["1111100"] = ebreakCw;

        // wfi - R-format (111111)
        ControlWord wfiCw;
        enableSignal(wfiCw, Signal::IS_WFI);
        enableSignal(wfiCw, Signal::IS_STALL);
        controlStore["1111110"] = wfiCw;
    }

public:
    MicroprogrammedControlUnit() {
        initializeControlStore();
    }

    // Allows dynamic modifications to the stored microprogram memory (Flexibility Feature)
    void updateMicrocode(const std::string& opcode, const ControlWord& newWord) {
        controlStore[opcode] = newWord;
    }

    // Decodes 32-bit binary instruction string into corresponding control bitset
    bool decodeInstruction(const std::string& instruction, ControlWord& outWord) const {
        if (instruction.length() < 7) {
            return false;
        }

        std::string opcode = instruction.substr(0, 7);
        auto it = controlStore.find(opcode);
        if (it != controlStore.end()) {
            outWord = it->second;
            return true;
        }
        return false; // Illegal Opcode
    }
};

int main() {
    MicroprogrammedControlUnit mcu;

    // Test cases covering distinct instruction families
    std::vector<std::pair<std::string, std::string>> testInstructions = {
        {"add",    "00000000001000100001100000000000"}, // ALU: Add
        {"fsub",   "01001000001000100001100000000000"}, // FPU: Floating Subtraction
        {"st",     "10000100001000100000000000000100"}, // Memory: Store
        {"beq",    "11000100000000000000000000001000"}, // Branch: Branch if Equal
        {"clrz",   "10110100001000000000000000000000"}, // System: Clear Zero
        {"ebreak", "11111000000000000000000000000000"}  // System Trap: Debug Break
    };

    std::cout << "Control Word Vector Length: " << CONTROL_WORD_WIDTH << " bits\n\n";
    std::cout << std::left << std::setw(12) << "Instruction" 
              << std::setw(10) << "Opcode" 
              << "Control Vector Bitset (36 bits)" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;

    for (const auto& test : testInstructions) {
        ControlWord cw;
        bool status = mcu.decodeInstruction(test.second, cw);

        if (status) {
            std::cout << std::left << std::setw(12) << test.first 
                      << std::setw(10) << test.second.substr(0, 6) 
                      << cw.to_string() << std::endl;
        } else {
            std::cout << std::left << std::setw(12) << test.first 
                      << std::setw(10) << test.second.substr(0, 6) 
                      << "UNKNOWN OPCODE" << std::endl;
        }
    }

    return 0;
}