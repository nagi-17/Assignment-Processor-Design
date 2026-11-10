#ifndef RISC201_ENGINE_SESSION_HPP
#define RISC201_ENGINE_SESSION_HPP

// ---------------------------------------------------------------------------
// Uniform handle to an execution engine for the simulator's inspection
// commands.  Two engines implement it:
//   * the production control-ROM pipeline (4-stage / 6-stage)
//   * the standalone microprogrammed-control-unit model
//
// Keeping the CPU headers behind the factories lets main.cpp drive either
// engine without including both latch families in one translation unit.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

class EngineSession {
public:
    virtual ~EngineSession() = default;

    virtual void load(const std::vector<std::string>& program) = 0;
    virtual bool step() = 0;
    virtual void run(std::uint64_t max_cycles = 1000000) = 0;
    virtual void reset() = 0;

    virtual std::uint64_t cycles() const = 0;
    virtual bool halted() const = 0;

    // full_listing: the micro view dumps every microprogram instead of only
    // the programs currently in flight.  Ignored by the control-ROM view.
    virtual void printView(std::ostream& os, bool full_listing) const = 0;
};

std::unique_ptr<EngineSession> makeFourStageControlRomSession();
std::unique_ptr<EngineSession> makeSixStageControlRomSession();
std::unique_ptr<EngineSession> makeMicroprogramSession(bool four_stage);

class SessionManager {
public:
    void invalidate() {
        rom_.reset();
        micro_.reset();
    }

    EngineSession& controlRom(bool four, const std::vector<std::string>& program) {
        if (!rom_ || rom_four_ != four) {
            rom_ = four ? makeFourStageControlRomSession()
                        : makeSixStageControlRomSession();
            rom_->load(program);
            rom_four_ = four;
        }
        return *rom_;
    }

    EngineSession& micro(bool four, const std::vector<std::string>& program) {
        if (!micro_ || micro_four_ != four) {
            micro_ = makeMicroprogramSession(four);
            micro_->load(program);
            micro_four_ = four;
        }
        return *micro_;
    }

    bool romIsFour() const   { return rom_four_; }
    bool microIsFour() const { return micro_four_; }

private:
    std::unique_ptr<EngineSession> rom_;
    std::unique_ptr<EngineSession> micro_;
    bool rom_four_{true};
    bool micro_four_{true};
};

#endif // RISC201_ENGINE_SESSION_HPP
