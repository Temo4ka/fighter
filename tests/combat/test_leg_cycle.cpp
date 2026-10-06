#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <ranges>
#include <utility>
#include <vector>

#include "anim/clip.hpp"
#include "combat/leg_cycle.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;

/// A lift that is 0 (both feet down) on [0.1, 0.2] and [0.5, 0.6] of a 0.8 s
/// cycle and 0.1 m elsewhere.
float getTwoSpanLift(float TimeSec) {
    const bool Down = (TimeSec >= 0.1f && TimeSec <= 0.2f) || (TimeSec >= 0.5f && TimeSec <= 0.6f);
    return Down ? 0.0f : 0.1f;
}

/// The first span has the left foot in front, the second the right one.
LegCycle makeTwoSpanCycle() {
    std::vector<SupportSpan> Spans = findSupportSpans(0.8f, getTwoSpanLift, 0.01f, 0.01f);
    REQUIRE(Spans.size() == 2);
    Spans[1].FrontFoot = BodyPart::FootR;
    return LegCycle(0.8f, std::move(Spans));
}

/// Walks \p Cycle forwards at rate 1 until its phase is about \p TimeSec.
void walkTo(LegCycle& Cycle, float TimeSec) {
    for (int Step = 0; Step < 200 && std::abs(Cycle.getTime() - TimeSec) > 0.005f; ++Step) {
        Cycle.walk(0.005f, 1.0f, 1.0f);
    }
    REQUIRE(Cycle.getTime() == Approx(TimeSec).margin(0.006f));
}

rig::RigSetup makeSetup() {
    const stats::PhysicalProfile Profile = stats::computeProfile({}, {}, stats::BalanceTable::getDefaults());
    rig::RigSetup Setup;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    return Setup;
}

} // namespace

TEST_CASE("findSupportSpans: runs of both feet down, also across the end", "[combat]") {
    const std::vector<SupportSpan> Spans = findSupportSpans(0.8f, getTwoSpanLift, 0.01f, 0.01f);
    REQUIRE(Spans.size() == 2);
    CHECK(Spans[0].BeginSec == Approx(0.1f).margin(0.011f));
    CHECK(Spans[0].EndSec == Approx(0.2f).margin(0.011f));
    CHECK(Spans[1].BeginSec == Approx(0.5f).margin(0.011f));

    // A span through the end of the period wraps: begin > end.
    const auto Wrapping = [](float TimeSec) { return TimeSec < 0.05f || TimeSec > 0.7f ? 0.0f : 0.1f; };
    const std::vector<SupportSpan> Wrapped = findSupportSpans(0.8f, Wrapping, 0.01f, 0.01f);
    REQUIRE(Wrapped.size() == 1);
    CHECK(Wrapped[0].BeginSec > Wrapped[0].EndSec);

    // Never both feet down: one zero-length span at the lowest sample.
    const auto Never = [](float TimeSec) { return 0.05f + std::abs(TimeSec - 0.3f); };
    const std::vector<SupportSpan> Lowest = findSupportSpans(0.8f, Never, 0.01f, 0.01f);
    REQUIRE(Lowest.size() == 1);
    CHECK(Lowest[0].BeginSec == Approx(0.3f).margin(0.011f));
    CHECK(Lowest[0].EndSec == Lowest[0].BeginSec);
}

TEST_CASE("LegCycle: stops at once with both feet down, else plays on to the nearest span", "[combat]") {
    LegCycle Cycle = makeTwoSpanCycle();
    CHECK_FALSE(Cycle.isPlaying());
    CHECK(Cycle.getFrontFoot() == BodyPart::FootL);
    CHECK(Cycle.getTime() == Approx(0.15f).margin(0.01f));

    SECTION("inside a span: holds there") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        CHECK(Cycle.getMode() == LegCycle::Mode::Still);
        CHECK(Cycle.getTime() == Approx(0.55f).margin(0.006f));
        CHECK(Cycle.getFrontFoot() == BodyPart::FootR);
        CHECK(Cycle.isEngaged());
    }
    SECTION("a foot in the air: forwards to the nearer span, quickly") {
        walkTo(Cycle, 0.42f);
        Cycle.stop(Dt, 3.0f);
        CHECK(Cycle.isStopping());
        CHECK(Cycle.getTime() == Approx(0.42f + 3.0f * Dt).margin(0.006f));
        for (int Step = 0; Step < 10 && Cycle.isPlaying(); ++Step) Cycle.stop(Dt, 3.0f);
        CHECK_FALSE(Cycle.isPlaying());
        CHECK(Cycle.getTime() >= 0.5f);
        CHECK(Cycle.getTime() <= 0.6f);
        CHECK(Cycle.getFrontFoot() == BodyPart::FootR);
        // Resting there: it stays engaged (the legs rest in its pose).
        CHECK(Cycle.isEngaged());
    }
    SECTION("or backwards, when the span behind is nearer") {
        walkTo(Cycle, 0.24f);
        for (int Step = 0; Step < 10 && Cycle.isPlaying(); ++Step) Cycle.stop(Dt, 3.0f);
        CHECK(Cycle.getTime() <= 0.2f);
        CHECK(Cycle.getFrontFoot() == BodyPart::FootL);
    }
    SECTION("walking again continues from the held phase") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        const float Held = Cycle.getTime();
        Cycle.walk(0.01f, 1.0f, 1.0f);
        CHECK(Cycle.getTime() == Approx(Held + 0.01f).margin(1e-4f));
    }
    SECTION("settle goes to the span with the front foot asked for and disengages") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        Cycle.settle();
        CHECK(Cycle.getFrontFoot() == BodyPart::FootL);
        CHECK(Cycle.getTime() == Approx(Cycle.getRestTime(BodyPart::FootL)));
        CHECK_FALSE(Cycle.isEngaged());
        Cycle.settle(BodyPart::FootR);
        CHECK(Cycle.getFrontFoot() == BodyPart::FootR);
        CHECK(Cycle.getTime() == Approx(0.55f).margin(0.011f));
    }
}

TEST_CASE("makeLegCycle: the walk rests in two wide double supports, one per front foot", "[combat]") {
    physics::World PhysWorld;
    const std::filesystem::path Data = FIGHTER_DATA_DIR;
    const rig::Rig Body(PhysWorld, rig::loadRigDef(Data / "rigs" / "humanoid.json"), makeSetup());
    const anim::Clip Walk = anim::loadClip(Data / "poses" / "walk.json");
    const anim::Pose Stance = anim::sampleClip(anim::loadClip(Data / "poses" / "stance.json"), 0.0f);
    constexpr float MinSpread = 0.25f;
    const LegCycle Cycle = makeLegCycle(Walk, Stance, Body, MinSpread);
    REQUIRE(Cycle.getSpans().size() == 2);

    // One with the left foot in front, one with the right; in both the feet
    // are down and wide apart all through the span.
    CHECK(Cycle.getSpans()[0].FrontFoot != Cycle.getSpans()[1].FrontFoot);
    const auto measure = [&](float TimeSec) {
        anim::Pose Pose = Stance;
        anim::layerPose(Pose, anim::sampleClip(Walk, TimeSec));
        return Body.measureLegs(Pose.Angles);
    };
    for (const SupportSpan& Span : Cycle.getSpans()) {
        const float Width = Span.EndSec >= Span.BeginSec ? Span.EndSec - Span.BeginSec
                                                         : Span.EndSec + Walk.DurationSec - Span.BeginSec;
        for (float Into = 0.0f; Into <= Width; Into += SupportSampleSec) {
            const rig::LegStance Legs = measure(std::fmod(Span.BeginSec + Into, Walk.DurationSec));
            INFO("t=" << Span.BeginSec + Into);
            CHECK(Legs.Left.SoleHeight <= Body.getControl().FootPlantHeight);
            CHECK(Legs.Right.SoleHeight <= Body.getControl().FootPlantHeight);
            CHECK(Legs.getSpread() >= MinSpread);
            CHECK(Legs.getFrontFoot() == Span.FrontFoot);
        }
    }
    // Where the feet pass each other they are not wide: a larger spread
    // leaves fewer (or narrower) spans, never more.
    const LegCycle Narrow = makeLegCycle(Walk, Stance, Body, 0.0f);
    CHECK(Narrow.getSpans().size() >= Cycle.getSpans().size());

    // The crouch walk too, over the crouch.
    anim::Pose Crouched = Stance;
    anim::layerPose(Crouched, anim::sampleClip(anim::loadClip(Data / "poses" / "crouch.json"), 0.0f));
    const LegCycle Crouch = makeLegCycle(anim::loadClip(Data / "poses" / "crouch_walk.json"), Crouched, Body, 0.0f);
    CHECK(Crouch.getSpans().size() == 2);
}

TEST_CASE("LegCycle: follow keeps the share of the last step", "[combat]") {
    LegCycle Cycle = makeTwoSpanCycle();
    walkTo(Cycle, 0.3f);
    const float Before = Cycle.getTime();
    Cycle.walk(0.1f, 1.0f, 1.0f);
    CHECK(Cycle.getStepFromTime() == Approx(Before));
    CHECK(Cycle.getTime() == Approx(Before + 0.1f));
    Cycle.follow(0.25f);
    CHECK(Cycle.getTime() == Approx(Before + 0.025f));
    CHECK(Cycle.getStepFromTime() == Approx(Before));
    // Backwards, across the start of the cycle.
    walkTo(Cycle, 0.02f);
    Cycle.walk(0.1f, 1.0f, -1.0f);
    CHECK(Cycle.getTime() == Approx(0.72f).margin(0.01f));
    Cycle.follow(0.5f);
    CHECK(Cycle.getTime() == Approx(0.77f).margin(0.01f));
    // A stop is no step to follow.
    Cycle.stop(0.01f, 3.0f);
    const float Stopping = Cycle.getTime();
    CHECK(Cycle.getStepFromTime() == Approx(Stopping));
    Cycle.follow(0.0f);
    CHECK(Cycle.getTime() == Approx(Stopping));
}

TEST_CASE("LegCycle: hold keeps a mid-step pose, or stops with both feet down", "[combat]") {
    LegCycle Cycle = makeTwoSpanCycle();
    walkTo(Cycle, 0.35f);   // between the spans: a foot in the air
    const float MidStep = Cycle.getTime();
    Cycle.hold();
    CHECK(Cycle.isHeld());
    CHECK_FALSE(Cycle.isPlaying());
    CHECK(Cycle.getTime() == MidStep);
    Cycle.hold();
    CHECK(Cycle.getTime() == MidStep);
    // Walking again goes on from there.
    Cycle.walk(0.01f, 1.0f, 1.0f);
    CHECK_FALSE(Cycle.isHeld());
    CHECK(Cycle.getTime() == Approx(MidStep + 0.01f));
    // With both feet down it simply stands.
    walkTo(Cycle, 0.55f);
    Cycle.hold();
    CHECK_FALSE(Cycle.isHeld());
    CHECK(Cycle.getMode() == LegCycle::Mode::Still);
    // settle() ends a hold.
    walkTo(Cycle, 0.35f);
    Cycle.hold();
    Cycle.settle();
    CHECK_FALSE(Cycle.isHeld());
}

TEST_CASE("LegCycle: a stop heads to the target chosen for it", "[combat]") {
    LegCycle Cycle = makeTwoSpanCycle();
    walkTo(Cycle, 0.3f);   // between the spans: a foot in the air
    const std::vector<StopTarget> Targets = Cycle.getStopTargets();
    REQUIRE(Targets.size() == 2);
    // Into each span the nearer way round: back into the first, on into the
    // second.
    CHECK(Targets[0].Span == 0);
    CHECK(Targets[0].Offset < 0.0f);
    CHECK(Targets[1].Span == 1);
    CHECK(Targets[1].Offset > 0.0f);
    // Unchosen the stop goes to the nearer one (back); chosen, to the other.
    Cycle.chooseStop(Targets[1]);
    CHECK(Cycle.getStopTarget() == Approx(Targets[1].TimeSec).margin(1e-4f));
    CHECK(Cycle.getFrontFoot() == BodyPart::FootR);
    for (int Step = 0; Step < 20 && Cycle.getMode() != LegCycle::Mode::Still; ++Step) Cycle.stop(Dt, 3.0f);
    CHECK(Cycle.getTime() >= 0.5f);
    CHECK(Cycle.getTime() <= 0.6f);
    // Resting in a span there is nothing to choose.
    CHECK(Cycle.getStopTargets().empty());
    // Walking again forgets the choice.
    walkTo(Cycle, 0.7f);
    for (int Step = 0; Step < 20 && Cycle.getMode() != LegCycle::Mode::Still; ++Step) Cycle.stop(Dt, 3.0f);
    CHECK(Cycle.getFrontFoot() == BodyPart::FootR);
    CHECK(Cycle.getTime() <= 0.6f);
}

TEST_CASE("LegCycle: a released walk coasts to the span ahead with the travel, or rests short", "[combat]") {
    SECTION("coast") {
        LegCycle Cycle = makeTwoSpanCycle();
        walkTo(Cycle, 0.42f);   // a foot in the air; the span ahead begins at 0.5
        Cycle.beginStop();
        CHECK(Cycle.isStopping());
        CHECK_FALSE(Cycle.isInSpan());
        const float Left = Cycle.getStopLeft();
        CHECK(Left == Approx(0.5f + 0.025f - 0.42f).margin(0.01f));   // a quarter into the span
        CHECK(Cycle.getLeftToSpanAhead() == Approx(Left).margin(0.01f));
        // Travel takes it there (no faster: rate 1), then it rests.
        Cycle.coast(0.05f, 1.0f, 1.0f);
        CHECK(Cycle.isStopping());
        CHECK(Cycle.getTime() == Approx(0.47f).margin(0.01f));
        CHECK(Cycle.getStepFromTime() == Approx(0.42f).margin(0.01f));
        // Travel the other way does not move it.
        Cycle.coast(0.05f, 1.0f, -1.0f);
        CHECK(Cycle.getTime() == Approx(0.47f).margin(0.01f));
        Cycle.coast(0.5f, 1.0f, 1.0f);
        CHECK(Cycle.getMode() == LegCycle::Mode::Still);
        CHECK(Cycle.isInSpan());
        CHECK(Cycle.getStopLeft() == 0.0f);
    }
    SECTION("in a span: it rests at once") {
        LegCycle Cycle = makeTwoSpanCycle();
        walkTo(Cycle, 0.55f);
        Cycle.beginStop();
        CHECK(Cycle.getMode() == LegCycle::Mode::Still);
        CHECK(Cycle.getLeftToSpanAhead() == 0.0f);
    }
    SECTION("backwards: the span behind is the one ahead") {
        LegCycle Cycle = makeTwoSpanCycle();
        walkTo(Cycle, 0.3f);
        Cycle.walk(0.01f, 1.0f, -1.0f);
        CHECK(Cycle.getDirection() < 0.0f);
        Cycle.beginStop();
        CHECK(Cycle.getStopTarget() <= 0.2f);
    }
    SECTION("rest: a short step rests where it is") {
        LegCycle Cycle = makeTwoSpanCycle();
        walkTo(Cycle, 0.3f);
        Cycle.beginStop();
        Cycle.rest();
        CHECK(Cycle.getMode() == LegCycle::Mode::Still);
        CHECK(Cycle.getTime() == Approx(0.3f).margin(0.006f));
        CHECK_FALSE(Cycle.isInSpan());
    }
}

TEST_CASE("LegCycle: steps from span middle to span middle", "[combat]") {
    LegCycle Cycle = makeTwoSpanCycle();
    CHECK_FALSE(Cycle.findStep(0.3f));   // no steps set
    Cycle.setSteps({{.BeginSec = 0.15f, .EndSec = 0.55f, .Swing = BodyPart::FootR},
                    {.BeginSec = 0.55f, .EndSec = 0.95f, .Swing = BodyPart::FootL}});
    CHECK(Cycle.findStep(0.3f) == 0u);
    CHECK(Cycle.findStep(0.7f) == 1u);
    CHECK(Cycle.findStep(0.05f) == 1u);   // the second step wraps through the end
    CHECK(Cycle.getStepShare(0, 0.35f) == Approx(0.5f));
    CHECK(Cycle.getStepShare(1, 0.05f) == Approx(0.75f));
    CHECK(Cycle.getStepShare(0, 0.10f) == Approx(-0.125f));   // a little before the begin
}

TEST_CASE("makeLegCycle: the walk's steps swing each foot once, in the air in the middle", "[combat]") {
    physics::World PhysWorld;
    const std::filesystem::path Data = FIGHTER_DATA_DIR;
    const rig::Rig Body(PhysWorld, rig::loadRigDef(Data / "rigs" / "humanoid.json"), makeSetup());
    const anim::Clip Walk = anim::loadClip(Data / "poses" / "walk.json");
    const anim::Pose Stance = anim::sampleClip(anim::loadClip(Data / "poses" / "stance.json"), 0.0f);
    const LegCycle Cycle = makeLegCycle(Walk, Stance, Body, 0.25f);
    REQUIRE(Cycle.getSteps().size() == 2);
    CHECK(Cycle.getSteps()[0].Swing != Cycle.getSteps()[1].Swing);
    for (const CycleStep& Step : Cycle.getSteps()) {
        CHECK(Step.EndSec > Step.BeginSec);
        CHECK(Step.LiftShare < Step.LandShare);
        // The swing foot goes from behind the pelvis to in front of it.
        CHECK(Step.SwingBeginX < 0.0f);
        CHECK(Step.SwingEndX > 0.0f);
    }
}
