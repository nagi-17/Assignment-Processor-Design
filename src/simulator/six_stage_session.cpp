// six_stage_session.cpp
// Control-ROM EngineSession backed by the production SixStageCPU.

#include "engine_session.hpp"
#include "six_stage_cpu.hpp"
#include "control_rom_session_impl.hpp"

std::unique_ptr<EngineSession> makeSixStageControlRomSession() {
    return std::make_unique<ControlRomSession<SixStageCPU, false>>();
}
