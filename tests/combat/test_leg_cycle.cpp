#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <ranges>
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

LegCycle makeTwoSpanCycle() {
    return LegCycle(0.8f, findSupportSpans(0.8f, getTwoSpanLift, 0.01f, 0.01f), 0);
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
    CHECK(Cycle.getVariant() == StanceVariant::Normal);
    CHECK(Cycle.getTime() == Approx(0.15f).margin(0.01f));

    SECTION("inside a span: holds there") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        CHECK(Cycle.getMode() == LegCycle::Mode::Still);
        CHECK(Cycle.getTime() == Approx(0.55f).margin(0.006f));
        CHECK(Cycle.getVariant() == StanceVariant::Switched);
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
        CHECK(Cycle.getVariant() == StanceVariant::Switched);
    }
    SECTION("or backwards, when the span behind is nearer") {
        walkTo(Cycle, 0.24f);
        for (int Step = 0; Step < 10 && Cycle.isPlaying(); ++Step) Cycle.stop(Dt, 3.0f);
        CHECK(Cycle.getTime() <= 0.2f);
        CHECK(Cycle.getVariant() == StanceVariant::Normal);
    }
    SECTION("walking again continues from the held phase") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        const float Held = Cycle.getTime();
        Cycle.walk(0.01f, 1.0f, 1.0f);
        CHECK(Cycle.getTime() == Approx(Held + 0.01f).margin(1e-4f));
    }
    SECTION("settle goes back to the normal stance and disengages") {
        walkTo(Cycle, 0.55f);
        Cycle.stop(Dt, 3.0f);
        Cycle.settle();
        CHECK(Cycle.getVariant() == StanceVariant::Normal);
        CHECK(Cycle.getTime() == Approx(Cycle.getNormalTime()));
        CHECK_FALSE(Cycle.isEngaged());
    }
}

TEST_CASE("makeLegCycle: the walk has two support phases, the normal one like the stance", "[combat]") {
    physics::World PhysWorld;
    const std::filesystem::path Data = FIGHTER_DATA_DIR;
    const rig::Rig Body(PhysWorld, rig::loadRigDef(Data / "rigs" / "humanoid.json"), makeSetup());
    const anim::Clip Walk = anim::loadClip(Data / "poses" / "walk.json");
    const anim::Pose Stance = anim::sampleClip(anim::loadClip(Data / "poses" / "stance.json"), 0.0f);
    const LegCycle Cycle = makeLegCycle(Walk, Stance, Body);
    REQUIRE(Cycle.getSpans().size() == 2);

    // In the normal one the left foot is in front, in the other the right.
    anim::Pose Normal = anim::sampleClip(Walk, Cycle.getNormalTime());
    CHECK(Normal.getAngle(BodyPart::ThighL) > Normal.getAngle(BodyPart::ThighR));
    for (const SupportSpan& Span : Cycle.getSpans()) {
        anim::Pose Pose = Stance;
        anim::layerPose(Pose, anim::sampleClip(Walk, (Span.BeginSec + Span.EndSec) * 0.5f));
        CHECK(Body.getSoleHeight(Pose.Angles, BodyPart::FootL) <= Body.getControl().FootPlantHeight);
        CHECK(Body.getSoleHeight(Pose.Angles, BodyPart::FootR) <= Body.getControl().FootPlantHeight);
    }

    // The crouch walk too, over the crouch.
    anim::Pose Crouched = Stance;
    anim::layerPose(Crouched, anim::sampleClip(anim::loadClip(Data / "poses" / "crouch.json"), 0.0f));
    const LegCycle Crouch = makeLegCycle(anim::loadClip(Data / "poses" / "crouch_walk.json"), Crouched, Body);
    CHECK(Crouch.getSpans().size() == 2);
}

TEST_CASE("getStanceVariantName: both variants have names", "[combat]") {
    CHECK(getStanceVariantName(StanceVariant::Normal) == "normal");
    CHECK(getStanceVariantName(StanceVariant::Switched) == "switched");
}
