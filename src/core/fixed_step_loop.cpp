#include "core/fixed_step_loop.hpp"

#include <algorithm>

namespace fighter {

FixedStepLoop::FixedStepLoop(Config config) : config_(config) {}

void FixedStepLoop::setPaused(bool paused) {
    paused_ = paused;
    singleStepRequested_ = false;
    // После снятия с паузы не догоняем время, накопленное до неё.
    accumulator_ = 0.0;
}

void FixedStepLoop::setTimeScale(double scale) {
    timeScale_ = std::clamp(scale, kMinTimeScale, kMaxTimeScale);
}

void FixedStepLoop::reset() {
    accumulator_ = 0.0;
    tick_ = 0;
    stepsLastAdvance_ = 0;
    singleStepRequested_ = false;
}

double FixedStepLoop::clampFrame(double frameSec) const {
    return std::clamp(frameSec, 0.0, config_.maxFrameSec);
}

} // namespace fighter
