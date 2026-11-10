#ifndef MICROCONTROL_EXECUTION_UNIT_HPP
#define MICROCONTROL_EXECUTION_UNIT_HPP

// ---------------------------------------------------------------------------
// Abstract microcontrol execution unit (μCEU).
//
// One instance sits behind each pipeline latch.  It owns a shared bus and a
// microsequencer, fetches the stage-local microprogram for the instruction key,
// and runs the whole microsequence inside one pipeline clock.  Derived units
// implement apply() (drive the bus / command a functional unit) and commit()
// (write the stage's output latch).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

#include "microinstruction.hpp"
#include "microprogram_store.hpp"
#include "microsequencer.hpp"
#include "shared_bus.hpp"

namespace mc {

class MicrocontrolExecutionUnit {
public:
    MicrocontrolExecutionUnit(const MicroprogramStore& store, Stage stage)
        : store_(store), stage_(stage) {}

    virtual ~MicrocontrolExecutionUnit() = default;

    void reset() {
        bus_.reset();
        seq_.reset();
        mir_ = Microinstruction{};
        trace_.clear();
        resetStage();
    }

    std::uint8_t microPC() const { return seq_.microPC(); }
    const Microinstruction& mir() const { return mir_; }
    const SharedBus& bus() const { return bus_; }
    const std::vector<Microinstruction>& lastTrace() const { return trace_; }
    Stage stage() const { return stage_; }

    // Runs a microprogram to completion.  Returns true if commit() ran.
    bool runProgram(const MicroProgram& program) {
        trace_.clear();

        if (program.empty()) {
            seq_.begin(0);
            commit();
            return true;
        }

        seq_.begin(program.size());
        while (seq_.valid()) {
            mir_ = program[seq_.microPC()];
            trace_.push_back(mir_);
            apply(mir_);

            if (mir_.seq == Seq::Repeat && mir_.repeat > 0) {
                if (seq_.repeat(mir_.repeat))
                    continue;               // repeat this micro-op
            }

            if (mir_.seq == Seq::Done) {
                commit();
                seq_.finish();
                return true;
            }
            seq_.next();
        }

        commit();
        seq_.finish();
        return true;
    }

protected:
    virtual void apply(const Microinstruction&) = 0;
    virtual void commit() = 0;
    virtual void resetStage() {}

    // Subclasses can use this to read a source through a stage-specific mux.
    virtual std::uint32_t readSource(BusDriver) = 0;

    const MicroprogramStore& store_;
    Stage stage_;
    SharedBus bus_;
    Microsequencer seq_;
    Microinstruction mir_{};
    std::vector<Microinstruction> trace_;
};

} // namespace mc

#endif // MICROCONTROL_EXECUTION_UNIT_HPP
