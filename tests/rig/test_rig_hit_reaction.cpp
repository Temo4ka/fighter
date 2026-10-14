#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <ranges>

#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;
/// A clean body kick: strength of the reaction levels, m/s.
constexpr float BodyKick = 0.3f;

RigSetup makeSetup(bool FacingRight);
void addFloor(physics::World& PhysWorld);
void simulate(physics::World& PhysWorld, Rig& Body, int Steps);

/// A rig standing with bent knees on a floor, settled.
struct Scene {
    physics::World PhysWorld;
    Rig Body;

    explicit Scene(bool FacingRight = true)
        : Body(PhysWorld, loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"),
               makeSetup(FacingRight)) {
        addFloor(PhysWorld);
        PerBodyPart<float> Angles{};
        // Both soles flat on the floor at the same height: the left knee
        // forward, the right shin back.
        Angles[static_cast<size_t>(BodyPart::ThighL)] = 0.3f;
        Angles[static_cast<size_t>(BodyPart::ShinL)] = -0.3f;
        Angles[static_cast<size_t>(BodyPart::ShinR)] = -0.3f;
        Angles[static_cast<size_t>(BodyPart::FootR)] = 0.3f;
        Body.setTargetAngles(Angles);
        Body.snapToTargets();
        simulate(PhysWorld, Body, 30);
    }
};

} // namespace

TEST_CASE("Rig: a body hit tilts and sinks the pelvis, then it settles", "[rig][pelvis_reaction]") {
    Scene Stand;
    Rig& Body = Stand.Body;
    const Vec2 PelvisAt = Body.getPartPosition(BodyPart::Pelvis);
    const float PelvisAngle = Body.getPartAngle(BodyPart::Pelvis);
    const Vec2 FootL = Body.getPartPosition(BodyPart::FootL);
    const Vec2 FootR = Body.getPartPosition(BodyPart::FootR);

    // Pushed back (to the left) a little above the pelvis.
    const auto Kick = Body.excitePelvis(BodyKick, {-1.0f, 0.0f}, PelvisAt + Vec2{0.05f, 0.25f});
    CHECK(Kick.Tilt > 0.0f);
    CHECK(Kick.Sink > 0.0f);
    float MaxTilt = 0.0f;
    float MaxDrop = 0.0f;
    float FootSlide = 0.0f;
    for (int Step = 0; Step < 40; ++Step) {
        simulate(Stand.PhysWorld, Body, 1);
        MaxTilt = std::max(MaxTilt, Body.getPartAngle(BodyPart::Pelvis) - PelvisAngle);
        MaxDrop = std::max(MaxDrop, PelvisAt.Y - Body.getPartPosition(BodyPart::Pelvis).Y);
        FootSlide = std::max({FootSlide, (Body.getPartPosition(BodyPart::FootL) - FootL).getLength(),
                              (Body.getPartPosition(BodyPart::FootR) - FootR).getLength()});
    }
    CHECK(MaxTilt == Approx(Kick.Tilt).epsilon(0.2));
    CHECK(MaxDrop == Approx(Kick.Sink).epsilon(0.2));
    // The planted feet stay where they stood (a pure tilt turns the hip
    // anchors a little about the pelvis).
    CHECK(FootSlide < 0.02f);

    simulate(Stand.PhysWorld, Body, 240);
    CHECK_FALSE(Body.getPelvisReaction().isActive());
    CHECK(Body.getPartAngle(BodyPart::Pelvis) == Approx(PelvisAngle).margin(1e-3));
    CHECK(Body.getPartPosition(BodyPart::Pelvis).Y == Approx(PelvisAt.Y).margin(1e-3));
}

TEST_CASE("Rig: a fighter facing left tilts back the other way", "[rig][pelvis_reaction]") {
    Scene Stand(false);
    const Vec2 PelvisAt = Stand.Body.getPartPosition(BodyPart::Pelvis);
    // Hit from the left, pushed to the right: the top goes right, clockwise.
    CHECK(Stand.Body.excitePelvis(BodyKick, {1.0f, 0.0f}, PelvisAt + Vec2{-0.05f, 0.3f}).Tilt < 0.0f);
}

TEST_CASE("Rig: a low hit buckles the knee of the leg it lands on", "[rig][pelvis_reaction]") {
    Scene Stand;
    Rig& Body = Stand.Body;
    const float KneeL = Body.getJointAngle(BodyPart::ShinL);
    const float KneeR = Body.getJointAngle(BodyPart::ShinR);
    const float FootAngle = Body.getPartAngle(BodyPart::FootL);

    const auto Kick = Body.excitePelvis(BodyKick, {-1.0f, 0.0f}, Body.getPartPosition(BodyPart::ShinL));
    CHECK(Kick.Leg < 2);
    CAPTURE(Kick.Leg, Body.isFootLocked(BodyPart::FootL), Body.isFootLocked(BodyPart::FootR));
    CHECK(Kick.Buckle > 0.0f);
    float BendL = 0.0f;
    float BendR = 0.0f;
    float HeelTurn = 0.0f;
    for (int Step = 0; Step < 20; ++Step) {
        simulate(Stand.PhysWorld, Body, 1);
        // A knee bends towards negative angles (facing right).
        BendL = std::max(BendL, KneeL - Body.getJointAngle(BodyPart::ShinL));
        BendR = std::max(BendR, KneeR - Body.getJointAngle(BodyPart::ShinR));
        HeelTurn = std::max(HeelTurn, FootAngle - Body.getPartAngle(BodyPart::FootL));
    }
    CHECK(Body.getPelvisReaction().getBuckle(Kick.Leg) >= 0.0f);
    CHECK(BendL > 0.05f);
    CHECK(BendL > BendR);
    // The heel lifts over the toes: the foot turns clockwise.
    CHECK(HeelTurn > 0.5f * Kick.Buckle);
    simulate(Stand.PhysWorld, Body, 240);
    CHECK(Body.getJointAngle(BodyPart::ShinL) == Approx(KneeL).margin(2e-3));
}

TEST_CASE("Rig: only a standing fighter gives way to a hit", "[rig][pelvis_reaction]") {
    Scene Stand;
    Rig& Body = Stand.Body;
    Body.applyHit(200.0f, Vec2{-1.0f, 0.0f}, Body.getCenterOfMass(), true);
    const auto Kick = Body.excitePelvis(BodyKick, {-1.0f, 0.0f}, Body.getPartPosition(BodyPart::Torso));
    CHECK(Kick.Tilt == 0.0f);
    CHECK(Kick.Sink == 0.0f);
    CHECK_FALSE(Body.getPelvisReaction().isActive());
}

TEST_CASE("Rig: the pelvis reaction is deterministic", "[rig][pelvis_reaction]") {
    Scene First;
    Scene Second;
    for (int Step = 0; Step < 90; ++Step) {
        if (Step % 25 == 0) {
            for (Scene* Each : {&First, &Second}) {
                Each->Body.excitePelvis(BodyKick, {-1.0f, 0.0f}, Each->Body.getPartPosition(BodyPart::ShinR));
            }
        }
        simulate(First.PhysWorld, First.Body, 1);
        simulate(Second.PhysWorld, Second.Body, 1);
        for (const BodyPart Part : {BodyPart::Pelvis, BodyPart::ShinR, BodyPart::FootR, BodyPart::Head}) {
            CHECK(First.Body.getPartPosition(Part) == Second.Body.getPartPosition(Part));
            CHECK(First.Body.getPartAngle(Part) == Second.Body.getPartAngle(Part));
        }
    }
}

namespace {

RigSetup makeSetup(bool FacingRight) {
    const stats::PhysicalProfile Profile =
        stats::computeProfile({.Constitution = 10}, {}, stats::BalanceTable::getDefaults());
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

} // namespace
