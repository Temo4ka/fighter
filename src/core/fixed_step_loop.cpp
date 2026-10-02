#include "core/fixed_step_loop.hpp"

#include <algorithm>

namespace fighter {

FixedStepLoop::FixedStepLoop(Config Settings) : Cfg(Settings) {}

void FixedStepLoop::setPaused(bool NewPaused) {
    Paused = NewPaused;
    SingleStepRequested = false;
    // After unpausing, do not catch up with the time accumulated before the pause.
    Accumulator = 0.0;
}

void FixedStepLoop::setTimeScale(double Scale) {
    TimeScale = std::clamp(Scale, MinTimeScale, MaxTimeScale);
}

void FixedStepLoop::reset() {
    Accumulator = 0.0;
    Tick = 0;
    StepsLastAdvance = 0;
    SingleStepRequested = false;
}

double FixedStepLoop::clampFrame(double FrameSec) const {
    return std::clamp(FrameSec, 0.0, Cfg.MaxFrameSec);
}

} // namespace fighter
