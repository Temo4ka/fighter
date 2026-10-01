#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/fixed_step_loop.hpp"

using fighter::FixedStepLoop;
using Catch::Approx;

namespace {
// 1/64 s and its multiples are exact in double, so the checks need no tolerance.
constexpr double Step = 1.0 / 64.0;
FixedStepLoop makeLoop() { return FixedStepLoop({.StepSec = Step, .MaxFrameSec = 0.25}); }
} // namespace

TEST_CASE("FixedStepLoop: step count does not depend on how time is split into frames", "[core][loop]") {
    FixedStepLoop Coarse = makeLoop();
    FixedStepLoop Fine = makeLoop();
    int CoarseSteps = 0, FineSteps = 0;

    for (int I = 0; I < 10; ++I) Coarse.advance(8 * Step, [&](double) { ++CoarseSteps; });
    for (int I = 0; I < 80 * 7; ++I) Fine.advance(Step / 7.0, [&](double) { ++FineSteps; });

    CHECK(CoarseSteps == 80);
    CHECK(FineSteps >= 79);   // Step / 7 is not exact, allow one step of error
    CHECK(FineSteps <= 80);
}

TEST_CASE("FixedStepLoop: dt always equals the step", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    int Steps = 0;
    Loop.advance(0.037, [&](double Dt) { CHECK(Dt == Step); ++Steps; });
    CHECK(Steps == 2);
}

TEST_CASE("FixedStepLoop: alpha is the leftover fraction of a step", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    const double Alpha = Loop.advance(2.5 * Step, [](double) {});
    CHECK(Loop.getTick() == 2);
    CHECK(Alpha == Approx(0.5));
}

TEST_CASE("FixedStepLoop: long frame is clamped", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    Loop.advance(10.0, [](double) {});
    CHECK(Loop.getTick() == 16);   // 0.25 s / (1/64 s)
}

TEST_CASE("FixedStepLoop: pause and single step", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    Loop.setPaused(true);
    CHECK(Loop.advance(1.0, [](double) {}) == 1.0);
    CHECK(Loop.getTick() == 0);

    Loop.requestSingleStep();
    Loop.advance(1.0, [](double) {});
    CHECK(Loop.getTick() == 1);
    Loop.advance(1.0, [](double) {});
    CHECK(Loop.getTick() == 1);   // a step request is one-shot

    Loop.setPaused(false);
    Loop.advance(4 * Step, [](double) {});
    CHECK(Loop.getTick() == 5);
}

TEST_CASE("FixedStepLoop: time scale", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    Loop.setTimeScale(0.5);
    Loop.advance(8 * Step, [](double) {});
    CHECK(Loop.getTick() == 4);

    Loop.setTimeScale(100.0);
    CHECK(Loop.getTimeScale() == FixedStepLoop::MaxTimeScale);
    Loop.setTimeScale(0.0);
    CHECK(Loop.getTimeScale() == FixedStepLoop::MinTimeScale);
}

TEST_CASE("FixedStepLoop: reset clears the counter", "[core][loop]") {
    FixedStepLoop Loop = makeLoop();
    Loop.advance(3.5 * Step, [](double) {});
    Loop.reset();
    CHECK(Loop.getTick() == 0);
    CHECK(Loop.advance(0.5 * Step, [](double) {}) == Approx(0.5));   // the remainder is cleared too
}
