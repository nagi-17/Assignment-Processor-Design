#ifndef RISC201_CONTROL_ROM_SESSION_IMPL_HPP
#define RISC201_CONTROL_ROM_SESSION_IMPL_HPP

// Template body for the control-ROM EngineSession.  Included from the
// 4-stage and 6-stage session .cpp files, each of which supplies its own CPU
// type (the two latch families cannot share one translation unit).

#include <ostream>
#include <vector>

#include "control_rom_view.hpp"
#include "engine_session.hpp"

template <typename Cpu, bool FourStage>
class ControlRomSession : public EngineSession {
public:
    void load(const std::vector<std::string>& program) override {
        program_ = program;
        cpu_.loadProgram(program_);
    }

    bool step() override {
        if (cpu_.getSnapshot().is_stopped) return false;
        return cpu_.stepCycle();
    }

    void run(std::uint64_t max_cycles) override {
        while (cpu_.totalCycles() < max_cycles &&
               !cpu_.getSnapshot().is_stopped) {
            if (!cpu_.stepCycle()) break;
        }
    }

    void reset() override { cpu_.loadProgram(program_); }

    std::uint64_t cycles() const override { return cpu_.totalCycles(); }
    bool halted() const override { return cpu_.getSnapshot().is_stopped; }

    void printView(std::ostream& os, bool /*full_listing*/) const override {
        os << "\n==== Control-ROM Pipeline (" << (FourStage ? "4-stage" : "6-stage")
           << ") - cycle " << cpu_.totalCycles() << " ====\n";
        os << "  retired=" << cpu_.instructionsRetired()
           << "  stalls=" << cpu_.stallCycles()
           << "  halted=" << (cpu_.getSnapshot().is_stopped ? "yes" : "no")
           << "\n\n";

        control_rom_view::printControlRomPipeline(os, cpu_.getSnapshot());
        control_rom_view::printControlRomListing(os);
    }

    Cpu& cpu() { return cpu_; }

private:
    Cpu cpu_{};
    std::vector<std::string> program_;
};

#endif // RISC201_CONTROL_ROM_SESSION_IMPL_HPP
