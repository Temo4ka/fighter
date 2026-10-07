#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <ranges>

#include "anim/clip.hpp"
#include "combat/leg_step.hpp"
#include "combat/tuning.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;
constexpr float StepSec = 0.12f;

/// Both feet flat on the floor: the left one in front.
rig::LegStance makeStance(float LeftX, float RightX) {
    rig::LegStance Stance{.PelvisHeight = 0.93f};
    Stance.Left = {.Ankle = {LeftX, 0.1f}, .Angle = 0.0f, .SoleHeight = 0.0f, .Planted = true};
    Stance.Right = {.Ankle = {RightX, 0.1f}, .Angle = 0.0f, .SoleHeight = 0.0f, .Planted = true};
    return Stance;
}

/// Runs \p Step to its end (the feet planted where it puts them).
void finish(LegStep& Step, const rig::LegStance& Target) {
    for (int Tick = 0; Tick < 60 && Step.isActive(); ++Tick) Step.advance(Dt, Step.getStance(Target));
}

rig::RigSetup makeSetup() {
    const stats::PhysicalProfile Profile = stats::computeProfile({}, {}, stats::BalanceTable::getDefaults());
    rig::RigSetup Setup;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    return Setup;
}

} // namespace

TEST_CASE("LegStep: legs already in the action's pose do not step", "[combat][legstep]") {
    const LegStepTuning Tuning;
    const rig::LegStance Stance = makeStance(0.2f, -0.25f);
    rig::LegStance Close = Stance;
    Close.Left.Ankle.X += Tuning.MinDistance * 0.5f;
    for (const bool Strike : {true, false}) {
        CHECK_FALSE(LegStep::plan(Stance, Close, true, false, Strike, StepSec, Tuning).isActive());
    }
    // No time for a step: none.
    rig::LegStance Far = Stance;
    Far.Left.Ankle.X += 0.2f;
    CHECK_FALSE(LegStep::plan(Stance, Far, true, false, true, 0.0f, Tuning).isActive());
    CHECK(LegStep().describe() == "-");
}

TEST_CASE("LegStep: a strike lifts its foot on an arc, the support foot stays", "[combat][legstep]") {
    const LegStepTuning Tuning;
    const rig::LegStance Now = makeStance(0.3f, -0.3f);
    // The kick wants the left foot 10 cm back and the right one 5 cm in:
    // only the kicking (clip) leg steps.
    rig::LegStance Target = makeStance(0.2f, -0.25f);
    Target.PelvisHeight = 0.95f;
    LegStep Step = LegStep::plan(Now, Target, true, false, true, StepSec, Tuning);
    REQUIRE(Step.isActive());
    REQUIRE(Step.getSteps()[0]);
    CHECK(Step.getSteps()[0]->Foot == BodyPart::FootL);
    CHECK_FALSE(Step.getSteps()[1]);

    float Highest = 0.0f;
    while (Step.isActive()) {
        Step.advance(Dt, Step.getStance(Target));
        if (!Step.isActive()) break;
        const rig::LegStance At = Step.getStance(Target);
        CHECK(Step.getSteppingFoot() == BodyPart::FootL);
        // The moving foot is in the air, the other one where it stood.
        CHECK(At.Left.SoleHeight > 0.0f);
        CHECK(At.Right.Ankle.X == Now.Right.Ankle.X);
        CHECK(At.Right.SoleHeight == 0.0f);
        // Between the start and the target, the pelvis too.
        CHECK(At.Left.Ankle.X <= Now.Left.Ankle.X);
        CHECK(At.Left.Ankle.X >= Target.Left.Ankle.X);
        CHECK(At.PelvisHeight >= Now.PelvisHeight);
        CHECK(At.PelvisHeight <= Target.PelvisHeight);
        Highest = std::max(Highest, At.Left.SoleHeight);
    }
    CHECK(Highest == Approx(Tuning.LiftHeight).margin(0.01f));
    CHECK(Step.getElapsedSec() == Approx(StepSec).margin(Dt));
    // Over: the action has the legs as it poses them.
    const rig::LegStance End = Step.getStance(Target);
    CHECK(End.Left.Ankle.X == Target.Left.Ankle.X);
    CHECK(End.PelvisHeight == Target.PelvisHeight);
}

TEST_CASE("LegStep: a foot in the air lands first, then the feet off their place step", "[combat][legstep]") {
    const LegStepTuning Tuning;
    // Mid-stride: the right foot in the air behind, the left one planted.
    rig::LegStance Now = makeStance(0.1f, -0.35f);
    Now.Right.Ankle.Y = 0.2f;
    Now.Right.SoleHeight = 0.1f;
    Now.Right.Planted = false;
    const rig::LegStance Target = makeStance(0.25f, -0.2f);
    // A crouch (no strike): both legs are the clip's.
    LegStep Step = LegStep::plan(Now, Target, true, true, false, StepSec, Tuning);
    REQUIRE(Step.isActive());
    REQUIRE(Step.getSteps()[0]);
    REQUIRE(Step.getSteps()[1]);
    CHECK(Step.getSteps()[0]->Foot == BodyPart::FootR);
    CHECK(Step.getSteps()[1]->Foot == BodyPart::FootL);
    CHECK_FALSE(Step.getSteps()[2]);
    CHECK(Step.getSteps()[0]->EndSec == Approx(StepSec * 0.5f));
    CHECK(Step.describe() == "step FootR 0.00, then FootL");

    // While the right foot lands the left one stands; then they swap.
    for (int Tick = 0; Tick < 60 && Step.isActive(); ++Tick) {
        Step.advance(Dt, Step.getStance(Target));
        if (!Step.isActive()) break;
        const rig::LegStance At = Step.getStance(Target);
        const std::optional<BodyPart> Moving = Step.getSteppingFoot();
        REQUIRE(Moving);
        const BodyPart Standing = *Moving == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
        CHECK(At.getFoot(Standing).SoleHeight <= 0.0f);
        CHECK(Step.getStepProgress() >= 0.0f);
        CHECK(Step.getStepProgress() < 1.0f);
    }
    finish(Step, Target);
    CHECK_FALSE(Step.isActive());
}

TEST_CASE("LegStep: a strike lands a support foot caught in the air before the kick", "[combat][legstep]") {
    const LegStepTuning Tuning;
    rig::LegStance Now = makeStance(0.2f, -0.1f);
    Now.Right.SoleHeight = 0.08f;
    Now.Right.Ankle.Y = 0.18f;
    Now.Right.Planted = false;
    const rig::LegStance Target = makeStance(0.2f, -0.25f);
    LegStep Step = LegStep::plan(Now, Target, true, false, true, StepSec, Tuning);
    REQUIRE(Step.isActive());
    // The support foot lands where the start pose has it, then the kicking
    // foot catches up with the clip.
    REQUIRE(Step.getSteps()[0]);
    REQUIRE(Step.getSteps()[1]);
    CHECK(Step.getSteps()[0]->Foot == BodyPart::FootR);
    REQUIRE(Step.getSteps()[0]->To);
    CHECK(Step.getSteps()[0]->To->Ankle.X == Target.Right.Ankle.X);
    CHECK(Step.getSteps()[1]->Foot == BodyPart::FootL);
    CHECK_FALSE(Step.getSteps()[1]->To);
}

TEST_CASE("LegStep: a foot that waits follows where the rig holds it", "[combat][legstep]") {
    const LegStepTuning Tuning;
    const rig::LegStance Now = makeStance(0.3f, -0.1f);
    const rig::LegStance Target = makeStance(0.2f, -0.25f);
    LegStep Step = LegStep::plan(Now, Target, true, true, false, StepSec, Tuning);
    REQUIRE(Step.getSteps()[1]);
    // The pelvis glides on 2 cm: the planted feet stay in the world, so
    // they are 2 cm further back relative to it.
    rig::LegStance Glided = Now;
    Glided.Left.Ankle.X -= 0.02f;
    Glided.Right.Ankle.X -= 0.02f;
    Step.advance(Dt, Glided);
    const BodyPart Waiting = Step.getSteps()[1]->Foot;
    CHECK(Step.getStance(Target).getFoot(Waiting).Ankle.X == Approx(Glided.getFoot(Waiting).Ankle.X));
    // A sole measured under the floor stands on it.
    rig::LegStance Sunk = Glided;
    Sunk.getFoot(Waiting).SoleHeight = -0.01f;
    Step.advance(Dt, Sunk);
    CHECK(Step.getStance(Target).getFoot(Waiting).SoleHeight == 0.0f);
    CHECK(Step.getStance(Target).getFoot(Waiting).Ankle.Y == Approx(Sunk.getFoot(Waiting).Ankle.Y + 0.01f));
}

TEST_CASE("LegStep: apply bends the legs of a pose to where the step has the feet", "[combat][legstep]") {
    physics::World PhysWorld;
    const std::filesystem::path Data = FIGHTER_DATA_DIR;
    const rig::Rig Body(PhysWorld, rig::loadRigDef(Data / "rigs" / "humanoid.json"), makeSetup());
    const anim::Pose Stance = anim::sampleClip(anim::loadClip(Data / "poses" / "stance.json"), 0.0f);
    const rig::LegStance Target = Body.measureLegs(Stance.Angles);
    rig::LegStance Now = Target;
    Now.Left.Ankle.X += 0.12f;
    const LegStepTuning Tuning;
    LegStep Step = LegStep::plan(Now, Target, true, false, true, StepSec, Tuning);
    REQUIRE(Step.isActive());
    for (int Tick = 0; Tick < 3; ++Tick) Step.advance(Dt, Step.getStance(Target));
    PerBodyPart<float> Angles = Stance.Angles;
    Step.apply(Angles, Body);
    const rig::LegStance Wanted = Step.getStance(Target);
    const rig::LegStance Got = Body.measureLegs(Angles);
    CHECK(Got.PelvisHeight == Approx(Wanted.PelvisHeight).margin(0.002f));
    CHECK(Got.Left.Ankle.X == Approx(Wanted.Left.Ankle.X).margin(0.002f));
    CHECK(Got.Left.Ankle.Y == Approx(Wanted.Left.Ankle.Y).margin(0.002f));
    CHECK(Got.Left.SoleHeight > 0.0f);
    CHECK(Got.Right.Ankle.X == Approx(Wanted.Right.Ankle.X).margin(0.002f));
    // Over, apply leaves the pose alone.
    finish(Step, Target);
    PerBodyPart<float> Untouched = Stance.Angles;
    Step.apply(Untouched, Body);
    CHECK(Untouched == Stance.Angles);
}
