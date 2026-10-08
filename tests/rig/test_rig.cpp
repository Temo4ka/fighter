#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <ranges>

#include "physics/world.hpp"
#include "rig/pelvis_controller.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;

RigDef loadHumanoid() { return loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

RigSetup makeSetup(bool FacingRight, int Constitution = 10) {
    const stats::PhysicalProfile Profile =
        stats::computeProfile({.Constitution = Constitution}, {}, stats::BalanceTable::getDefaults());
    RigSetup Setup;
    Setup.Origin = {0.0f, 0.005f};
    Setup.FacingRight = FacingRight;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    return Setup;
}

void addFloor(physics::World& PhysWorld) {
    const physics::Body Ground = PhysWorld.createBody({.Type = physics::BodyType::Static});
    PhysWorld.addShape(Ground,
                       {.Kind = physics::ShapeKind::Box, .Center = {0.0f, -0.5f}, .HalfExtents = {10.0f, 0.5f}});
}

void simulate(physics::World& PhysWorld, Rig& Body, int Steps) {
    for (int Step = 0; Step < Steps; ++Step) {
        Body.planMotion(Dt);
        Body.applyControl(Dt);
        PhysWorld.step(Dt);
    }
}

/// Target angles with a raised left arm, a bent left elbow and bent knees,
/// the rest at the reference pose.
PerBodyPart<float> makeTargets() {
    PerBodyPart<float> Angles{};
    Angles[static_cast<size_t>(BodyPart::UpperArmL)] = 0.5f;
    Angles[static_cast<size_t>(BodyPart::ForearmL)] = 1.2f;
    Angles[static_cast<size_t>(BodyPart::ThighL)] = 0.3f;
    Angles[static_cast<size_t>(BodyPart::ShinL)] = -0.4f;
    Angles[static_cast<size_t>(BodyPart::ShinR)] = -0.2f;
    return Angles;
}

/// A rig standing in the target pose on a floor.
struct Scene {
    physics::World PhysWorld;
    Rig Body;

    explicit Scene(const RigSetup& Setup) : Body(PhysWorld, loadHumanoid(), Setup) {
        addFloor(PhysWorld);
        Body.setTargetAngles(makeTargets());
        Body.snapToTargets();
    }
};

} // namespace

TEST_CASE("Rig: bodies get the masses of the profile", "[rig]") {
    physics::World PhysWorld;
    const RigSetup Setup = makeSetup(true);
    const Rig Body(PhysWorld, loadHumanoid(), Setup);

    float Expected = 0.0f;
    for (const auto& Mass : Setup.MassKg) Expected += Mass;
    // Kinematic parts have no mass in the solver, the rig keeps it.
    CHECK(Body.getTotalMass() == Approx(Expected).epsilon(0.001));
    CHECK(PhysWorld.getBodyCount() == static_cast<int>(BodyPartCount));
    CHECK(PhysWorld.getJointCount() == static_cast<int>(BodyPartCount) - 1);
}

TEST_CASE("Rig: a part strikes with its limb, the top of a group with the group", "[rig]") {
    const RigSetup Setup = makeSetup(true);
    const auto massOf = [&](std::initializer_list<BodyPart> Parts) {
        float Sum = 0.0f;
        for (const BodyPart Part : Parts) Sum += Setup.MassKg[static_cast<size_t>(Part)];
        return Sum;
    };
    using enum BodyPart;
    {
        physics::World PhysWorld;
        const Rig Body(PhysWorld, loadHumanoid(), Setup);
        // The posed group: a leg up to the pelvis, the pelvis with both legs.
        CHECK(Body.getStrikeMass(FootL) == Approx(massOf({FootL, ShinL, ThighL})));
        CHECK(Body.getStrikeMass(ThighR) == Approx(massOf({ThighR})));
        CHECK(Body.getStrikeMass(Pelvis) ==
              Approx(massOf({Pelvis, ThighL, ShinL, FootL, ThighR, ShinR, FootR})));
        // The physical group: an arm up to the torso, the head alone, the
        // torso with the head and both arms.
        CHECK(Body.getStrikeMass(ForearmL) == Approx(massOf({ForearmL, UpperArmL})));
        CHECK(Body.getStrikeMass(UpperArmR) == Approx(massOf({UpperArmR})));
        CHECK(Body.getStrikeMass(Head) == Approx(massOf({Head})));
        CHECK(Body.getStrikeMass(Torso) ==
              Approx(massOf({Torso, Head, UpperArmL, ForearmL, UpperArmR, ForearmR})));
    }
    {
        // A two-handed weapon strikes with both arms.
        RigSetup TwoHanded = Setup;
        TwoHanded.Held = {{.Part = ForearmR, .WeaponReachM = 0.9f}};
        TwoHanded.GripPart = ForearmL;
        physics::World PhysWorld;
        const Rig Body(PhysWorld, loadHumanoid(), TwoHanded);
        CHECK(Body.getStrikeMass(ForearmR) == Approx(massOf({ForearmR, UpperArmR, ForearmL, UpperArmL})));
        CHECK(Body.getStrikeMass(ForearmL) == Approx(massOf({ForearmL, UpperArmL})));
    }
}

TEST_CASE("Rig: the pelvis and the legs are kinematic, the rest is physical", "[rig]") {
    physics::World PhysWorld;
    const Rig Body(PhysWorld, loadHumanoid(), makeSetup(true));
    CHECK(Body.getPosture() == Posture::Standing);
    for (const auto Part : {BodyPart::Pelvis, BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR,
                            BodyPart::ShinR, BodyPart::FootR}) {
        CHECK(Body.isKinematic(Part));
    }
    for (const auto Part : {BodyPart::Torso, BodyPart::Head, BodyPart::UpperArmL, BodyPart::ForearmL,
                            BodyPart::UpperArmR, BodyPart::ForearmR}) {
        CHECK_FALSE(Body.isKinematic(Part));
    }
}

TEST_CASE("Rig: it stands on the floor and keeps the target pose", "[rig]") {
    Scene Stage(makeSetup(true));
    Rig& Body = Stage.Body;
    const Vec2 PelvisStart = Body.getPartPosition(BodyPart::Pelvis);
    simulate(Stage.PhysWorld, Body, 120);

    // The legs follow the targets exactly: they are posed, not driven.
    CHECK(Body.getJointAngle(BodyPart::ShinL) == Approx(-0.4f).margin(0.01f));
    CHECK(Body.getJointAngle(BodyPart::ThighL) == Approx(0.3f).margin(0.01f));
    // The motors bring the physical arm there.
    CHECK(Body.getJointAngle(BodyPart::ForearmL) == Approx(1.2f).margin(0.1f));
    CHECK(Body.getJointAngle(BodyPart::UpperArmL) == Approx(0.5f).margin(0.1f));
    // The lowest sole touches the floor and nothing drifts.
    CHECK(Body.getFloorPoint().Y == Approx(0.0f).margin(0.005f));
    CHECK(Body.getPartPosition(BodyPart::Pelvis).X == PelvisStart.X);
    CHECK(Body.getPartPosition(BodyPart::Pelvis).Y == Approx(PelvisStart.Y).margin(1e-4f));
    CHECK(Body.getPartPosition(BodyPart::Head).Y > 1.5f);
}

TEST_CASE("Rig: the pelvis follows the controller", "[rig]") {
    Scene Stage(makeSetup(true));
    Rig& Body = Stage.Body;
    const float StartX = Body.getPartPosition(BodyPart::Pelvis).X;
    Body.setMoveVelocity(1.0f);
    simulate(Stage.PhysWorld, Body, 60);

    // After the acceleration the walking speed is the requested one.
    CHECK(Body.getController().getVelocity() == Approx(1.0f));
    CHECK(Body.getPartPosition(BodyPart::Pelvis).X == Approx(Body.getController().getPositionX()).margin(1e-3f));
    CHECK(Body.getPartPosition(BodyPart::Pelvis).X - StartX > 0.8f);
    CHECK(Body.getPartPosition(BodyPart::Head).Y > 1.5f);
}

TEST_CASE("Rig: a fighter facing left is a mirror image", "[rig]") {
    Scene Right(makeSetup(true));
    Scene Left(makeSetup(false));
    simulate(Right.PhysWorld, Right.Body, 60);
    simulate(Left.PhysWorld, Left.Body, 60);

    CHECK_FALSE(Left.Body.isFacingRight());
    // Joint angles are reported unmirrored, as in a pose.
    const float Elbow = Right.Body.getJointAngle(BodyPart::ForearmL);
    CHECK(Left.Body.getJointAngle(BodyPart::ForearmL) == Approx(Elbow).margin(0.05f));
    for (const auto Part : {BodyPart::ForearmL, BodyPart::FootL}) {
        const Vec2 RightPos = Right.Body.getPartPosition(Part);
        const Vec2 LeftPos = Left.Body.getPartPosition(Part);
        CHECK(RightPos.X > 0.0f);
        CHECK(LeftPos.X == Approx(-RightPos.X).margin(0.03f));
        CHECK(LeftPos.Y == Approx(RightPos.Y).margin(0.03f));
    }
}

TEST_CASE("Rig: a hit lowers stiffness, which then recovers", "[rig]") {
    physics::World PhysWorld;
    const RigDef Def = loadHumanoid();
    Rig Body(PhysWorld, Def, makeSetup(true));
    CHECK(Body.getStiffness() == 1.0f);

    // Enough to reach the floor of the stiffness, too weak to knock down.
    const float Impulse = 1.0f / Def.Control.StiffnessPerImpulse;
    REQUIRE(Impulse / Body.getTotalMass() < Def.Control.KnockdownSpeed);
    Body.applyHit(Impulse, 1.0f);
    CHECK(Body.getPosture() == Posture::Standing);
    CHECK(Body.getStiffness() == Approx(Def.Control.MinStiffness));

    const auto RecoverySteps = static_cast<int>(1.0f / Def.Control.StiffnessRecovery / Dt) + 1;
    for (int Step = 0; Step < RecoverySteps; ++Step) Body.applyControl(Dt);
    CHECK(Body.getStiffness() == 1.0f);

    // An attack raises the base stiffness; a hit scales it down.
    Body.setBaseStiffness(1.5f);
    CHECK(Body.getStiffness() == 1.5f);
}

TEST_CASE("Rig: knockback is the impulse over the mass of the whole fighter", "[rig]") {
    physics::World PhysWorld;
    const RigDef Def = loadHumanoid();
    Rig Light(PhysWorld, Def, makeSetup(true, 10));
    Rig Heavy(PhysWorld, Def, makeSetup(true, 25));
    REQUIRE(Heavy.getTotalMass() > Light.getTotalMass() * 1.3f);

    constexpr float Impulse = 20.0f;
    Light.applyHit(Impulse, 1.0f);
    Heavy.applyHit(Impulse, -1.0f);
    CHECK(Light.getController().getKnockback() ==
          Approx(Impulse * Def.Control.KnockbackScale / Light.getTotalMass()));
    CHECK(Heavy.getController().getKnockback() ==
          Approx(-Impulse * Def.Control.KnockbackScale / Heavy.getTotalMass()));
}

TEST_CASE("Rig: a strong hit knocks the fighter down, then it gets up", "[rig]") {
    Scene Stage(makeSetup(true));
    Rig& Body = Stage.Body;
    const RigDef Def = loadHumanoid();
    simulate(Stage.PhysWorld, Body, 10);

    Body.applyHit(Def.Control.KnockdownSpeed * Body.getTotalMass() / Def.Control.KnockbackScale, -1.0f);
    REQUIRE(Body.getPosture() == Posture::KnockedDown);
    for (size_t Index = 0; Index < BodyPartCount; ++Index) CHECK_FALSE(Body.isKinematic(static_cast<BodyPart>(Index)));
    CHECK(Body.getStiffness() < 1.0f);

    const auto DownSteps = static_cast<int>(std::ceil(Def.Control.KnockdownSec / Dt));
    float LowestHead = Body.getPartPosition(BodyPart::Head).Y;
    for (int Step = 0; Step < DownSteps - 1; ++Step) {
        simulate(Stage.PhysWorld, Body, 1);
        LowestHead = std::min(LowestHead, Body.getPartPosition(BodyPart::Head).Y);
    }
    CHECK(LowestHead < 0.6f);   // fell
    CHECK(Body.getPosture() == Posture::KnockedDown);

    simulate(Stage.PhysWorld, Body, 2);
    CHECK(Body.getPosture() == Posture::GettingUp);
    CHECK(Body.isKinematic(BodyPart::Pelvis));

    simulate(Stage.PhysWorld, Body, static_cast<int>(std::ceil(Def.Control.GetUpSec / Dt)) + 1);
    CHECK(Body.getPosture() == Posture::Standing);
    CHECK(Body.getFloorPoint().Y == Approx(0.0f).margin(0.005f));
    simulate(Stage.PhysWorld, Body, 60);   // the motors bring the upper body up
    CHECK(Body.getPartPosition(BodyPart::Head).Y > 1.5f);
    CHECK(Body.getStiffness() == 1.0f);
}

TEST_CASE("Rig: the caller may decide about the knockdown", "[rig]") {
    const RigDef Def = loadHumanoid();
    // Strong enough for the rig's own threshold, but combat says no.
    Scene Strong(makeSetup(true));
    simulate(Strong.PhysWorld, Strong.Body, 10);
    const float Impulse = 2.0f * Def.Control.KnockdownSpeed * Strong.Body.getTotalMass() / Def.Control.KnockbackScale;
    Strong.Body.applyHit(Impulse, 1.0f, false);
    CHECK(Strong.Body.getPosture() == Posture::Standing);
    CHECK(Strong.Body.getController().getKnockback() > 0.0f);

    // A weak hit that combat makes a knockdown.
    Scene Weak(makeSetup(true));
    simulate(Weak.PhysWorld, Weak.Body, 10);
    Weak.Body.applyHit(1.0f, -1.0f, true);
    CHECK(Weak.Body.getPosture() == Posture::KnockedDown);
}

TEST_CASE("PelvisController: walking accelerates to the target speed", "[rig]") {
    PelvisController Controller(1.0f, {.WalkAcceleration = 6.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.5f);
    Controller.plan(0.1f);
    CHECK(Controller.getWalkVelocity() == Approx(0.6f));
    CHECK(Controller.getPlannedX() == Approx(1.06f));
    CHECK(Controller.getPositionX() == 1.0f);   // nothing moves before commit
    Controller.commit(0.1f);
    CHECK(Controller.getPositionX() == Approx(1.06f));
    CHECK(Controller.getVelocity() == Approx(0.6f));

    for (int Step = 0; Step < 10; ++Step) {
        Controller.plan(0.1f);
        Controller.commit(0.1f);
    }
    CHECK(Controller.getVelocity() == Approx(1.5f));
}

TEST_CASE("PelvisController: stopping uses the deceleration", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 6.0f, .WalkDeceleration = 20.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.2f);
    for (int Step = 0; Step < 10; ++Step) {
        Controller.plan(0.1f);
        Controller.commit(0.1f);
    }
    CHECK(Controller.getWalkVelocity() == Approx(1.2f));
    Controller.setTargetVelocity(0.0f);
    Controller.plan(0.05f);
    CHECK(Controller.getWalkVelocity() == Approx(0.2f));   // 1.2 - 20 * 0.05
    Controller.commit(0.05f);
    Controller.plan(0.05f);
    CHECK(Controller.getWalkVelocity() == 0.0f);
    // Turning round brakes first, at the deceleration.
    Controller.setTargetVelocity(1.0f);
    Controller.plan(0.1f);
    Controller.setTargetVelocity(-1.0f);
    Controller.plan(0.01f);
    CHECK(Controller.getWalkVelocity() == Approx(0.6f - 0.2f));
}

TEST_CASE("PelvisController: capWalkTravel ends a coast at its distance", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 100.0f, .WalkDeceleration = 20.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.0f);
    Controller.plan(0.1f);
    Controller.commit(0.1f);
    Controller.addKnockback(0.5f);
    Controller.plan(0.1f);
    // Walk 0.1 m and knockback 0.05 m planned; the walk may go 0.02 m.
    CHECK_FALSE(Controller.capWalkTravel(0.5f, 0.1f));
    CHECK(Controller.capWalkTravel(0.02f, 0.1f));
    CHECK(Controller.getPlannedTravel() == Approx(0.02f + 0.05f));
    CHECK(Controller.getWalkVelocity() == 0.0f);
    // Backwards too, and a negative limit is none.
    PelvisController Back(0.0f, {.WalkAcceleration = 100.0f, .WalkDeceleration = 20.0f, .KnockbackDecay = 5.0f});
    Back.setTargetVelocity(-1.0f);
    Back.plan(0.1f);
    CHECK(Back.capWalkTravel(-1.0f, 0.1f));
    CHECK(Back.getPlannedTravel() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("PelvisController: knockback decays and walls stop it", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 6.0f, .KnockbackDecay = 5.0f});
    Controller.addKnockback(2.0f);
    Controller.plan(0.1f);
    CHECK(Controller.getPlannedX() == Approx(0.2f));
    CHECK(Controller.getKnockback() == Approx(2.0f * std::exp(-0.5f)));
    Controller.commit(0.1f);

    // The total distance is about speed / decay.
    for (int Step = 0; Step < 200; ++Step) {
        Controller.plan(0.01f);
        Controller.commit(0.01f);
    }
    CHECK(Controller.getPositionX() == Approx(0.2f + 2.0f * std::exp(-0.5f) / 5.0f).margin(0.01f));

    Controller.addKnockback(3.0f);
    Controller.plan(0.1f);
    Controller.limit(-1.0f, 0.5f);
    CHECK(Controller.getPlannedX() == 0.5f);
    CHECK(Controller.getKnockback() == 0.0f);

    Controller.shift(-0.2f);
    Controller.commit(0.1f);
    CHECK(Controller.getPositionX() == Approx(0.3f));
}

TEST_CASE("Rig: getSoleHeight lifts a bent leg's foot off the floor", "[rig]") {
    Scene Setup(makeSetup(true));
    PerBodyPart<float> Angles{};
    // Both legs straight: both soles on the floor.
    CHECK(Setup.Body.getSoleHeight(Angles, BodyPart::FootL) == Approx(0.0f).margin(0.002f));
    CHECK(Setup.Body.getSoleHeight(Angles, BodyPart::FootR) == Approx(0.0f).margin(0.002f));
    // The left knee bent: the right leg carries the body, the left foot is up.
    Angles[static_cast<size_t>(BodyPart::ShinL)] = -1.2f;
    CHECK(Setup.Body.getSoleHeight(Angles, BodyPart::FootR) == Approx(0.0f).margin(0.002f));
    CHECK(Setup.Body.getSoleHeight(Angles, BodyPart::FootL) > 0.1f);
    // The pose of the rig itself is not changed by the probe.
    CHECK(Setup.Body.getSoleHeight(makeTargets(), BodyPart::FootR) == Approx(0.0f).margin(0.002f));
}

TEST_CASE("PelvisController: the planned travel and the share of it made", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 100.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.0f);
    Controller.plan(0.1f);
    CHECK(Controller.getPlannedTravel() == Approx(0.1f));
    // The share of a travel that ending at a point makes, clamped to [0, 1].
    CHECK(Controller.getTravelShare(0.05f, 0.1f) == Approx(0.5f));
    CHECK(Controller.getTravelShare(-0.05f, 0.1f) == 0.0f);
    CHECK(Controller.getTravelShare(0.3f, 0.1f) == 1.0f);
    CHECK(Controller.getTravelShare(0.3f, 0.0f) == 1.0f);   // no travel
    CHECK(Controller.getTravelShare(-0.05f, -0.1f) == Approx(0.5f));
}

TEST_CASE("PelvisController: the clip's travel is planned motion", "[rig][pelvis]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 100.0f, .KnockbackDecay = 5.0f});
    Controller.setClipTravel(0.02f);
    Controller.plan(0.1f);
    CHECK(Controller.getPlannedX() == Approx(0.02f));
    CHECK(Controller.getPlannedTravel() == Approx(0.02f));
    CHECK(Controller.getPlannedClipTravel() == Approx(0.02f));
    Controller.commit(0.1f);
    CHECK(Controller.getPositionX() == Approx(0.02f));
    CHECK(Controller.getClipTravelMade() == Approx(0.02f));
    CHECK(Controller.getClipVelocity() == Approx(0.2f));
    CHECK(Controller.getVelocity() == Approx(0.2f));
    // Used once: the next step plans none.
    Controller.plan(0.1f);
    CHECK(Controller.getPlannedClipTravel() == 0.0f);
    Controller.commit(0.1f);
    CHECK(Controller.getClipTravelMade() == 0.0f);

    // A correction takes it back like a walk: half of it is made.
    Controller.setClipTravel(0.04f);
    Controller.plan(0.1f);
    Controller.shift(-0.02f);
    Controller.commit(0.1f);
    CHECK(Controller.getClipTravelMade() == Approx(0.02f));
    // A wall stops it.
    Controller.setClipTravel(0.04f);
    Controller.plan(0.1f);
    Controller.limit(-1.0f, Controller.getPositionX());
    Controller.commit(0.1f);
    CHECK(Controller.getClipTravelMade() == Approx(0.0f).margin(1e-6f));
    Controller.reset(0.0f);
    CHECK(Controller.getClipVelocity() == 0.0f);
}

TEST_CASE("PelvisController: corrections are told by their source", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 100.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.0f);
    Controller.plan(0.1f);
    Controller.shift(-0.03f);
    Controller.limit(-1.0f, 0.05f);
    CHECK(Controller.getSpacingShift() == Approx(-0.03f));
    CHECK(Controller.getWallShift() == Approx(-0.02f));
    Controller.commit(0.1f);
    // Kept after the commit (for the panel), cleared by the next plan.
    CHECK(Controller.getSpacingShift() == Approx(-0.03f));
    Controller.plan(0.1f);
    CHECK(Controller.getSpacingShift() == 0.0f);
    CHECK(Controller.getWallShift() == 0.0f);

    Controller.setSpacingMotion({.Slowed = -0.5f, .Pushed = 0.2f, .Eased = 0.1f, .Wall = 0.05f, .Overlap = 0.01f});
    CHECK(Controller.getSpacingMotion().Pushed == 0.2f);
    CHECK(Controller.getSpacingMotion().Overlap == 0.01f);
}

TEST_CASE("PelvisController: the push-out is knockback the panel shows apart", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 6.0f, .KnockbackDecay = 5.0f});
    Controller.addKnockback(1.0f);
    Controller.addPushOut(0.5f);
    CHECK(Controller.getKnockback() == Approx(1.5f));
    CHECK(Controller.getPushOut() == Approx(0.5f));
    Controller.plan(0.1f);
    CHECK(Controller.getPlannedX() == Approx(0.15f));
    CHECK(Controller.getPushOut() == Approx(0.5f * std::exp(-0.5f)));
    // A wall stops it like the knockback.
    Controller.limit(-1.0f, 0.1f);
    CHECK(Controller.getPushOut() == 0.0f);
}

TEST_CASE("PelvisController: a walk held back picks up again with its acceleration", "[rig]") {
    PelvisController Controller(0.0f, {.WalkAcceleration = 6.0f, .KnockbackDecay = 5.0f});
    Controller.setTargetVelocity(1.2f);
    for (int Step = 0; Step < 30; ++Step) {
        Controller.plan(0.1f);
        Controller.commit(0.1f);
    }
    REQUIRE(Controller.getWalkVelocity() == Approx(1.2f));
    Controller.slowWalk(0.2f);
    CHECK(Controller.getWalkVelocity() == Approx(0.2f));
    // Never below 0, never faster than it was.
    Controller.slowWalk(-0.5f);
    CHECK(Controller.getWalkVelocity() == 0.0f);
    Controller.slowWalk(3.0f);
    CHECK(Controller.getWalkVelocity() == 0.0f);
    Controller.plan(0.1f);
    CHECK(Controller.getWalkVelocity() == Approx(0.6f));
}
