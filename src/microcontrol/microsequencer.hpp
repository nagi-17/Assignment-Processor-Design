#ifndef MICROCONTROL_MICROSEQUENCER_HPP
#define MICROCONTROL_MICROSEQUENCER_HPP

// ---------------------------------------------------------------------------
// Stage-local microsequencer.
//
// The sequencer walks a MicroProgram from microPC = 0 to the first Seq::Done.
// The whole walk happens inside one pipeline clock, so the pipeline still
// advances one stage per CPU clock.  microPC is kept as observable state for
// the debugger (`micro` command).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace mc {

class Microsequencer {
public:
    void reset() {
        micro_pc_ = 0;
        size_ = 0;
        repeat_ = 0;
        done_ = true;
    }

    void begin(std::size_t program_size) {
        micro_pc_ = 0;
        size_ = program_size;
        repeat_ = 0;
        done_ = (program_size == 0);
    }

    bool valid() const { return !done_ && micro_pc_ < size_; }
    bool done() const  { return done_; }
    std::uint8_t microPC() const { return micro_pc_; }
    std::size_t size() const     { return size_; }

    void next() {
        if (micro_pc_ + 1 < size_)
            ++micro_pc_;
        else
            done_ = true;
    }

    // Repeat the current micro-op `extra` more times before advancing.
    bool repeat(std::uint8_t extra) {
        if (repeat_ < extra) {
            ++repeat_;
            return true;      // stay on this micro-op
        }
        repeat_ = 0;
        return false;
    }

    void finish() {
        done_ = true;
        micro_pc_ = static_cast<std::uint8_t>(size_ == 0 ? 0 : size_ - 1);
    }

private:
    std::uint8_t micro_pc_{0};
    std::size_t  size_{0};
    std::uint8_t repeat_{0};
    bool         done_{true};
};

} // namespace mc

#endif // MICROCONTROL_MICROSEQUENCER_HPP
