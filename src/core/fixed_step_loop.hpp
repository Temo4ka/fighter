//===- core/fixed_step_loop.hpp - Fixed-timestep loop -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares FixedStepLoop, the accumulator that turns variable frame
/// times into simulation steps of constant length (docs/DEVELOPMENT_PLAN.md,
/// section 3.2).
///
/// Physics always receives the same dt, so the simulation does not depend on
/// frame rate and is reproducible. The loop also implements the debug time
/// controls: pause, single step and slow motion.
///
/// \code
///   double Alpha = Loop.advance(FrameSec, [&](double Dt) { Battle.update(..., Dt); });
///   Renderer.draw(Prev, Curr, Alpha);
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>

namespace fighter {

class FixedStepLoop {
public:
    struct Config {
        double StepSec = 1.0 / 60.0;
        /// Frames longer than this (debugger break, window drag) are clamped.
        /// Otherwise the simulation would try to catch up with hundreds of
        /// steps in a row (the "spiral of death").
        double MaxFrameSec = 0.25;
    };

    static constexpr double MinTimeScale = 0.1;
    static constexpr double MaxTimeScale = 1.0;

    FixedStepLoop() : FixedStepLoop(Config{}) {}
    explicit FixedStepLoop(Config C);

    /// Advances time by \p FrameSec seconds of real time and calls
    /// \p OnStep(dt) as many times as needed.
    ///
    /// \returns alpha in [0, 1]: how far rendering should move from the
    /// previous state towards the current one.
    template <std::invocable<double> StepFn>
    double advance(double FrameSec, StepFn&& OnStep) {
        StepsLastAdvance = 0;

        if (Paused) {
            if (SingleStepRequested) {
                SingleStepRequested = false;
                runStep(OnStep);
            }
            return 1.0;   // While paused, show exactly the current state.
        }

        Accumulator += clampFrame(FrameSec) * TimeScale;
        while (Accumulator >= Cfg.StepSec) {
            runStep(OnStep);
            Accumulator -= Cfg.StepSec;
        }
        return Accumulator / Cfg.StepSec;
    }

    /// \name Time controls (used by the debug build)
    /// @{
    void setPaused(bool NewPaused);
    bool isPaused() const { return Paused; }
    /// While paused, run exactly one step on the next advance().
    void requestSingleStep() { SingleStepRequested = true; }
    /// Slow motion: the step dt stays the same, only the real-time rate changes.
    void setTimeScale(double Scale);
    double getTimeScale() const { return TimeScale; }
    /// @}

    double getStepSec() const { return Cfg.StepSec; }
    std::uint64_t getTick() const { return Tick; }
    int getStepsLastAdvance() const { return StepsLastAdvance; }

    /// Resets the step counter and accumulated time (fight restart).
    void reset();

private:
    template <class StepFn>
    void runStep(StepFn& OnStep) {
        OnStep(Cfg.StepSec);
        ++Tick;
        ++StepsLastAdvance;
    }

    double clampFrame(double FrameSec) const;

    Config Cfg;
    double Accumulator = 0.0;
    double TimeScale = 1.0;
    bool Paused = false;
    bool SingleStepRequested = false;
    std::uint64_t Tick = 0;
    int StepsLastAdvance = 0;
};

} // namespace fighter
