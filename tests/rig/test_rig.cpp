#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
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

RigDef loadHumanoid() { return loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

RigSetup makeSetup(bool FacingRight) {
    const stats::PhysicalProfile Profile = stats::computeProfile({}, {}, stats::BalanceTable::getDefaults());
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
        Body.applyControl(Dt);
        PhysWorld.step(Dt);
    }
}

/// Target angles with a bent left elbow and knees, the rest at the reference pose.
PerBodyPart<float> makeTargets() {
    PerBodyPart<float> Angles{};
    Angles[static_cast<size_t>(BodyPart::UpperArmL)] = 0.5f;
    Angles[static_cast<size_t>(BodyPart::ForearmL)] = 1.2f;
    Angles[static_cast<size_t>(BodyPart::ShinL)] = -0.2f;
    Angles[static_cast<size_t>(BodyPart::ShinR)] = -0.2f;
    return Angles;
}

} // namespace

TEST_CASE("Rig: bodies get the masses of the profile", "[rig]") {
    physics::World PhysWorld;
    const RigSetup Setup = makeSetup(true);
    const Rig Body(PhysWorld, loadHumanoid(), Setup);

    float Expected = 0.0f;
    for (const auto& Mass : Setup.MassKg) Expected += Mass;
    CHECK(Body.getTotalMass() == Approx(Expected).epsilon(0.001));
    CHECK(PhysWorld.getBodyCount() == static_cast<int>(BodyPartCount));
    CHECK(PhysWorld.getJointCount() == static_cast<int>(BodyPartCount) - 1);
}

TEST_CASE("Rig: motors drive the joints to the target pose", "[rig]") {
    physics::World PhysWorld;
    addFloor(PhysWorld);
    Rig Body(PhysWorld, loadHumanoid(), makeSetup(true));
    Body.setTargetAngles(makeTargets());
    simulate(PhysWorld, Body, 120);

    CHECK(Body.getJointAngle(BodyPart::ForearmL) == Approx(1.2f).margin(0.1f));
    CHECK(Body.getJointAngle(BodyPart::UpperArmL) == Approx(0.5f).margin(0.1f));
    // The balance assists keep the body up on its own feet.
    CHECK(Body.isGrounded());
    CHECK(Body.getPartPosition(BodyPart::Head).Y > 1.5f);
}

TEST_CASE("Rig: a fighter facing left is a mirror image", "[rig]") {
    physics::World RightWorld;
    physics::World LeftWorld;
    addFloor(RightWorld);
    addFloor(LeftWorld);
    Rig FacingRight(RightWorld, loadHumanoid(), makeSetup(true));
    Rig FacingLeft(LeftWorld, loadHumanoid(), makeSetup(false));
    FacingRight.setTargetAngles(makeTargets());
    FacingLeft.setTargetAngles(makeTargets());
    simulate(RightWorld, FacingRight, 60);
    simulate(LeftWorld, FacingLeft, 60);

    CHECK_FALSE(FacingLeft.isFacingRight());
    // Joint angles are reported unmirrored, as in a pose.
    const float Elbow = FacingRight.getJointAngle(BodyPart::ForearmL);
    CHECK(FacingLeft.getJointAngle(BodyPart::ForearmL) == Approx(Elbow).margin(0.05f));
    const Vec2 RightHand = FacingRight.getPartPosition(BodyPart::ForearmL);
    const Vec2 LeftHand = FacingLeft.getPartPosition(BodyPart::ForearmL);
    CHECK(RightHand.X > 0.0f);
    CHECK(LeftHand.X == Approx(-RightHand.X).margin(0.03f));
    CHECK(LeftHand.Y == Approx(RightHand.Y).margin(0.03f));
}

TEST_CASE("Rig: a hit lowers stiffness, which then recovers", "[rig]") {
    physics::World PhysWorld;
    const RigDef Def = loadHumanoid();
    Rig Body(PhysWorld, Def, makeSetup(true));
    CHECK(Body.getStiffness() == 1.0f);

    Body.applyHit(1000.0f);   // far more than needed to reach the floor
    CHECK(Body.getStiffness() == Approx(Def.Control.MinStiffness));

    const auto RecoverySteps = static_cast<int>(1.0f / Def.Control.StiffnessRecovery / Dt) + 1;
    for (int Step = 0; Step < RecoverySteps; ++Step) Body.applyControl(Dt);
    CHECK(Body.getStiffness() == 1.0f);

    // An attack raises the base stiffness; a hit scales it down.
    Body.setBaseStiffness(1.5f);
    CHECK(Body.getStiffness() == 1.5f);
}
