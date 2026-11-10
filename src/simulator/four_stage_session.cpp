// four_stage_session.cpp
// Control-ROM EngineSession backed by the production FourStageCPU.

#include "engine_session.hpp"
#include "four_stage_cpu.hpp"
#include "control_rom_session_impl.hpp"

std::unique_ptr<EngineSession> makeFourStageControlRomSession() {
    return std::make_unique<ControlRomSession<FourStageCPU, true>>();
}
