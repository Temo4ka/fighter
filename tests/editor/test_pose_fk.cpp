#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>

#include "anim/pose.hpp"
#include "editor/pose_fk.hpp"
#include "rig/rig_def.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

const rig::RigDef& getRig() {
    static const rig::RigDef Def =
        rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json");
    return Def;
}

/// A short sword in the hand that holds weapons (the numbers of data/items).
rig::HeldItem makeSword() {
    return {.Part = getRig().Weapon.Part,
            .WeaponReachM = 0.55f,
            .WeaponWidthM = 0.06f,
            .WeaponAngleDeg = 85.0f};
}

/// The lowest point of the sole of a foot, m.
float getSoleHeight(const PosedBody& Body, BodyPart Foot) {
    const rig::PartDef& Part = getRig().getPart(Foot);
    float Lowest = std::numeric_limits<float>::max();
    for (const float SignX : {-1.0f, 1.0f}) {
        for (const float SignY : {-1.0f, 1.0f}) {
            const Vec2 Corner = Part.Center + Vec2{SignX * Part.HalfExtents.X, SignY * Part.HalfExtents.Y};
            const PosedPart& Posed = Body.get(Foot);
            Lowest = std::min(Lowest, Posed.Position.Y + rotate(Corner - getPartOrigin(Part), Posed.Angle).Y);
        }
    }
    return Lowest;
}

} // namespace

TEST_CASE("The zero pose puts every part where the rig file has it", "[editor][fk]") {
    const PosedBody Body = poseBody(getRig(), anim::Pose{});
    for (const auto& Part : getRig().Parts) {
        INFO(getBodyPartName(Part.Part));
        const Vec2 Origin = getPartOrigin(Part);
        CHECK(Body.get(Part.Part).Position.X == Approx(Origin.X).margin(1e-5));
        CHECK(Body.get(Part.Part).Position.Y == Approx(Origin.Y).margin(1e-5));
        CHECK(Body.get(Part.Part).Angle == Approx(0.0f).margin(1e-6));
    }
    // A capsule's origin is the middle of its spine: the torso goes from 1.15 to 1.38.
    CHECK(Body.get(BodyPart::Torso).Position.Y == Approx(1.265f));
    CHECK(Body.get(BodyPart::Head).Position.X == Approx(0.01f));
}

TEST_CASE("A raised arm swings about the shoulder", "[editor][fk]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::UpperArmL, 90.0f * RadiansPerDegree);   // forward and level
    const PosedBody Body = poseBody(getRig(), Pose);
    const PosedPart& Upper = Body.get(BodyPart::UpperArmL);
    // The shoulder is at (0, 1.42); the arm's middle is 0.125 m below it, now ahead of it.
    CHECK(Upper.Position.X == Approx(0.125f).margin(1e-5));
    CHECK(Upper.Position.Y == Approx(1.42f).margin(1e-5));
    CHECK(Upper.Angle == Approx(90.0f * RadiansPerDegree));
    // The forearm hangs from the elbow with the arm's turn: still pointing forward.
    const PosedPart& Fore = Body.get(BodyPart::ForearmL);
    CHECK(Fore.Angle == Approx(90.0f * RadiansPerDegree));
    CHECK(Fore.Position.Y == Approx(Upper.Position.Y).margin(0.03f));
    CHECK(Fore.Position.X > Upper.Position.X);
}

TEST_CASE("A bent elbow adds to the angle of the upper arm", "[editor][fk]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::UpperArmR, 30.0f * RadiansPerDegree);
    Pose.setAngle(BodyPart::ForearmR, 45.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose);
    CHECK(Body.get(BodyPart::ForearmR).Angle == Approx(75.0f * RadiansPerDegree));
}

TEST_CASE("Angles are clamped to the limits of the joint", "[editor][fk]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::ForearmL, -1.0f);   // the elbow does not bend backwards: [0, 150]
    Pose.setAngle(BodyPart::ShinL, 1.0f);       // nor the knee: [-150, 0]
    const PosedBody Body = poseBody(getRig(), Pose);
    CHECK(Body.get(BodyPart::ForearmL).Angle == Approx(0.0f).margin(1e-6));
    CHECK(Body.get(BodyPart::ShinL).Angle == Approx(Body.get(BodyPart::ThighL).Angle).margin(1e-6));
}

TEST_CASE("Joint ranges come from the rig, the root gets the lean range", "[editor][fk]") {
    const AngleRange Elbow = getJointRange(getRig(), BodyPart::ForearmL);
    CHECK(Elbow.Lower == Approx(0.0f));
    CHECK(Elbow.Upper == Approx(150.0f * RadiansPerDegree));
    const AngleRange Lean = getJointRange(getRig(), BodyPart::Pelvis);
    CHECK(Lean.Lower == Approx(-MaxLeanRad));
    CHECK(Lean.Upper == Approx(MaxLeanRad));
    const AngleRange Wrist = getWristRange(getRig());
    CHECK(Wrist.Upper == Approx(120.0f * RadiansPerDegree));
}

TEST_CASE("The body stands on the floor in a crouch", "[editor][fk]") {
    anim::Pose Pose;
    const std::array<std::pair<BodyPart, float>, 6> Crouch = {{{BodyPart::ThighL, 60.0f}, {BodyPart::ShinL, -95.0f},
                                                              {BodyPart::FootL, 35.0f}, {BodyPart::ThighR, 40.0f},
                                                              {BodyPart::ShinR, -110.0f}, {BodyPart::FootR, 35.0f}}};
    for (const auto& [Part, Degrees] : Crouch) {
        Pose.setAngle(Part, Degrees * RadiansPerDegree);
    }
    const PosedBody Body = poseBody(getRig(), Pose);
    const float Lowest = std::min(getSoleHeight(Body, BodyPart::FootL), getSoleHeight(Body, BodyPart::FootR));
    CHECK(Lowest == Approx(0.0f).margin(1e-4));
    // Bent knees bring the pelvis down.
    CHECK(Body.get(BodyPart::Pelvis).Position.Y < 0.98f - 0.1f);
}

TEST_CASE("The lean turns the whole body about the pelvis", "[editor][fk]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::Pelvis, -10.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose);
    CHECK(Body.get(BodyPart::Torso).Angle == Approx(-10.0f * RadiansPerDegree));
    CHECK(Body.get(BodyPart::Head).Angle == Approx(-10.0f * RadiansPerDegree));
    // Leaning forward (negative here) moves the head ahead of the pelvis.
    CHECK(Body.get(BodyPart::Head).Position.X > Body.get(BodyPart::Pelvis).Position.X + 0.1f);
}

TEST_CASE("A weapon hangs from the fist along the forearm", "[editor][fk]") {
    const rig::HeldItem Sword = makeSword();
    anim::Pose Pose;
    Pose.setWeaponAngle(0.0f);
    const PosedBody Body = poseBody(getRig(), Pose, std::span(&Sword, 1));
    REQUIRE(Body.Weapons.size() == 1);
    const PosedWeapon& Blade = Body.Weapons.front();
    CHECK(Blade.Holder == getRig().Weapon.Part);
    // The forearm ends at (0, 0.87); the blade continues it down: the holder radius
    // 0.045 + reach 0.55 - blade radius 0.03.
    CHECK(Blade.Fist.X == Approx(0.0f).margin(1e-5));
    CHECK(Blade.Fist.Y == Approx(0.87f).margin(1e-5));
    CHECK(Blade.Tip.X == Approx(0.0f).margin(1e-5));
    CHECK(Blade.Tip.Y == Approx(0.87f - 0.565f).margin(1e-4));
    CHECK(Blade.Radius == Approx(0.03f));
}

TEST_CASE("The wrist turns the weapon to the forearm", "[editor][fk]") {
    const rig::HeldItem Sword = makeSword();
    anim::Pose Pose;
    Pose.setWeaponAngle(90.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose, std::span(&Sword, 1));
    const PosedWeapon& Blade = Body.Weapons.front();
    // Turned counter-clockwise a quarter from "down": it points forward.
    CHECK(Blade.Tip.X == Approx(0.565f).margin(1e-4));
    CHECK(Blade.Tip.Y == Approx(0.87f).margin(1e-4));
    CHECK(Blade.WristAngle == Approx(90.0f * RadiansPerDegree));
}

TEST_CASE("The wrist is clamped and defaults to the item's angle", "[editor][fk]") {
    const rig::HeldItem Sword = makeSword();
    anim::Pose Far;
    Far.setWeaponAngle(179.0f * RadiansPerDegree);
    const PosedBody Clamped = poseBody(getRig(), Far, std::span(&Sword, 1));
    CHECK(Clamped.Weapons.front().WristAngle == Approx(getWristRange(getRig()).Upper));

    // A pose with no wrist leaves it to the item: 85 degrees.
    const PosedBody Default = poseBody(getRig(), anim::Pose{}, std::span(&Sword, 1));
    CHECK(Default.Weapons.front().WristAngle == Approx(85.0f * RadiansPerDegree));
}

TEST_CASE("The weapon follows its forearm", "[editor][fk]") {
    const rig::HeldItem Sword = makeSword();
    anim::Pose Pose;
    Pose.setAngle(getRig().Weapon.Part == BodyPart::ForearmL ? BodyPart::UpperArmL : BodyPart::UpperArmR,
                  90.0f * RadiansPerDegree);
    Pose.setWeaponAngle(0.0f);
    const PosedBody Body = poseBody(getRig(), Pose, std::span(&Sword, 1));
    // The arm points forward, the blade (wrist 0) continues it: the tip is far ahead.
    const PosedWeapon& Blade = Body.Weapons.front();
    CHECK(Blade.Tip.X - Blade.Fist.X == Approx(0.565f).margin(1e-4));
    CHECK(Blade.Tip.Y == Approx(Blade.Fist.Y).margin(1e-4));
}

TEST_CASE("A shield is a plate on its forearm", "[editor][fk]") {
    const rig::HeldItem Shield = {.Part = BodyPart::ForearmR, .ShieldLengthM = 0.5f, .ShieldWidthM = 0.1f};
    const PosedBody Body = poseBody(getRig(), anim::Pose{}, std::span(&Shield, 1));
    REQUIRE(Body.Shields.size() == 1);
    CHECK(Body.Weapons.empty());
    CHECK(Body.Shields.front().Center.Y == Approx(Body.get(BodyPart::ForearmR).Position.Y));
    CHECK(Body.Shields.front().HalfExtents.X == Approx(0.25f));
}
