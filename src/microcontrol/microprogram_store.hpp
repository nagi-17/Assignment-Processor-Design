#ifndef MICROCONTROL_MICROPROGRAM_STORE_HPP
#define MICROCONTROL_MICROPROGRAM_STORE_HPP

// ---------------------------------------------------------------------------
// Per-stage microprogram store.
//
// The store is the single source of truth for instruction behaviour in the
// microcontrol model.  It is keyed by the same 7-bit key the assembler emits
// (6-bit opcode + 1 format bit) and keeps a separate MicroProgram for every
// pipeline stage.  A legacy ControlWord is projected from the microcode so the
// model can still show the original horizontal control signals.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

#include "control_signals.hpp"
#include "microinstruction.hpp"

namespace mc {

class MicroprogramStore {
public:
    MicroprogramStore();

    // Microprogram for one stage and instruction key, or nullptr.
    const MicroProgram* find(Stage stage, uint16_t key) const;

    // Horizontal control word projected from the microcode (empty if unknown).
    ControlWord legacyWord(uint16_t key) const;

    bool hasKey(uint16_t key) const;
    std::size_t programCount() const { return programs_.size(); }
    std::size_t keyCount() const     { return legacy_.size(); }

    // Every instruction key present in the store, sorted, for listings.
    std::vector<uint16_t> keys() const;

    // Helpers shared with the assembler-style encoders/tests.
    static uint16_t keyOf(const std::string& mnemonic, bool imm_format);
    static uint16_t keyFromIR(uint32_t ir) { return static_cast<uint16_t>((ir >> 25) & 0x7F); }
    static std::string keyString(uint16_t key);

private:
    // composite key: (stage << 8) | instruction key
    static uint32_t composite(Stage stage, uint16_t key) {
        return (static_cast<uint32_t>(stage) << 8) | key;
    }

    void add(Stage stage, uint16_t key, MicroProgram program);
    void addLegacy(uint16_t key, std::initializer_list<Signal> signals);

    std::unordered_map<uint32_t, MicroProgram> programs_;
    std::unordered_map<uint16_t, ControlWord>  legacy_;
};

} // namespace mc

#endif // MICROCONTROL_MICROPROGRAM_STORE_HPP
