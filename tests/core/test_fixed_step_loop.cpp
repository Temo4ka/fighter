#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/fixed_step_loop.hpp"

using fighter::FixedStepLoop;
using Catch::Approx;

namespace {
// 1/64 с и кратные ей длительности точно представимы в double — проверки без погрешности.
constexpr double kStep = 1.0 / 64.0;
FixedStepLoop makeLoop() { return FixedStepLoop({.stepSec = kStep, .maxFrameSec = 0.25}); }
} // namespace

TEST_CASE("FixedStepLoop: step count does not depend on how time is split into frames", "[core][loop]") {
    FixedStepLoop coarse = makeLoop();
    FixedStepLoop fine = makeLoop();
    int coarseSteps = 0, fineSteps = 0;

    for (int i = 0; i < 10; ++i) coarse.advance(8 * kStep, [&](double) { ++coarseSteps; });
    for (int i = 0; i < 80 * 7; ++i) fine.advance(kStep / 7.0, [&](double) { ++fineSteps; });

    CHECK(coarseSteps == 80);
    CHECK(fineSteps >= 79);   // kStep / 7 не представимо точно — допускаем один шаг
    CHECK(fineSteps <= 80);
}

TEST_CASE("FixedStepLoop: dt always equals the step", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    int steps = 0;
    loop.advance(0.037, [&](double dt) { CHECK(dt == kStep); ++steps; });
    CHECK(steps == 2);
}

TEST_CASE("FixedStepLoop: alpha is the leftover fraction of a step", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    const double alpha = loop.advance(2.5 * kStep, [](double) {});
    CHECK(loop.tick() == 2);
    CHECK(alpha == Approx(0.5));
}

TEST_CASE("FixedStepLoop: long frame is clamped", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    loop.advance(10.0, [](double) {});
    CHECK(loop.tick() == 16);   // 0.25 с / (1/64 с)
}

TEST_CASE("FixedStepLoop: pause and single step", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    loop.setPaused(true);
    CHECK(loop.advance(1.0, [](double) {}) == 1.0);
    CHECK(loop.tick() == 0);

    loop.requestSingleStep();
    loop.advance(1.0, [](double) {});
    CHECK(loop.tick() == 1);
    loop.advance(1.0, [](double) {});
    CHECK(loop.tick() == 1);   // запрос на шаг одноразовый

    loop.setPaused(false);
    loop.advance(4 * kStep, [](double) {});
    CHECK(loop.tick() == 5);
}

TEST_CASE("FixedStepLoop: time scale", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    loop.setTimeScale(0.5);
    loop.advance(8 * kStep, [](double) {});
    CHECK(loop.tick() == 4);

    loop.setTimeScale(100.0);
    CHECK(loop.timeScale() == FixedStepLoop::kMaxTimeScale);
    loop.setTimeScale(0.0);
    CHECK(loop.timeScale() == FixedStepLoop::kMinTimeScale);
}

TEST_CASE("FixedStepLoop: reset clears the counter", "[core][loop]") {
    FixedStepLoop loop = makeLoop();
    loop.advance(3.5 * kStep, [](double) {});
    loop.reset();
    CHECK(loop.tick() == 0);
    CHECK(loop.advance(0.5 * kStep, [](double) {}) == Approx(0.5));   // остаток тоже сброшен
}
