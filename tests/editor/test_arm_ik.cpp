#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>

#include "anim/pose.hpp"
#include "editor/arm_ik.hpp"
#include "editor/pose_fk.hpp"
#include "rig/rig_def.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
constexpr BodyPart Upper = BodyPart::UpperArmL;
constexpr BodyPart Forearm = BodyPart::ForearmL;

const rig::RigDef& getRig() {
    static const rig::RigDef Def =
        rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json");
    return Def;
}

/// The humanoid with an elbow that bends both ways, -150..150 degrees.
const rig::RigDef& getDoubleJointedRig() {
    static const rig::RigDef Def = [] {
        rig::RigDef Copy = getRig();
        for (auto& Joint : Copy.Joints) {
            if (Joint.Child == Forearm) Joint.LowerAngle = -150.0f * RadiansPerDegree;
        }
        return Copy;
    }();
    return Def;
}

rig::HeldItem makeSword() {
    return {.Part = Forearm, .WeaponReachM = 0.55f, .WeaponWidthM = 0.06f, .WeaponAngleDeg = 85.0f};
}

/// The fist after posing the solved arm on \p Pose.
Vec2 poseFist(const rig::RigDef& Def, anim::Pose Pose, ArmAngles Angles) {
    Pose.setAngle(Upper, Angles.Upper);
    Pose.setAngle(Forearm, Angles.Forearm);
    return getFistPoint(Def, poseBody(Def, Pose), Forearm);
}

bool isInRange(float Angle, AngleRange Range) {
    return Angle >= Range.Lower - 1e-5f && Angle <= Range.Upper + 1e-5f;
}

void checkLimits(const rig::RigDef& Def, ArmAngles Angles) {
    CHECK(isInRange(Angles.Upper, getJointRange(Def, Upper)));
    CHECK(isInRange(Angles.Forearm, getJointRange(Def, Forearm)));
}

} // namespace

TEST_CASE("Pivots, fists and joint angles of a posed body", "[editor][ik]") {
    const PosedBody Zero = poseBody(getRig(), anim::Pose{});
    CHECK(getJointPivot(getRig(), Zero, Upper).X == Approx(0.0f).margin(1e-5));
    CHECK(getJointPivot(getRig(), Zero, Upper).Y == Approx(1.42f));
    CHECK(getFistPoint(getRig(), Zero, Forearm).Y == Approx(0.87f));

    anim::Pose Pose;
    Pose.setAngle(BodyPart::Torso, 10.0f * RadiansPerDegree);
    Pose.setAngle(Upper, 40.0f * RadiansPerDegree);
    Pose.setAngle(Forearm, 200.0f * RadiansPerDegree);   // clamped to 150
    const PosedBody Body = poseBody(getRig(), Pose);
    CHECK(getPosedJointAngle(getRig(), Body, Upper) == Approx(40.0f * RadiansPerDegree));
    CHECK(getPosedJointAngle(getRig(), Body, Forearm) == Approx(150.0f * RadiansPerDegree));
}

TEST_CASE("An angle is clamped to its range by the nearer limit", "[editor][ik]") {
    const AngleRange Range = {.Lower = -1.0f, .Upper = 2.9f};
    CHECK(clampJointAngle(0.5f, Range) == Approx(0.5f));
    CHECK(clampJointAngle(0.5f - 2.0f * std::numbers::pi_v<float>, Range) == Approx(0.5f));
    CHECK(clampJointAngle(-3.1f, Range) == Approx(2.9f));   // just past the upper limit the other way round
    CHECK(clampJointAngle(-1.5f, Range) == Approx(-1.0f));
}

TEST_CASE("The arm puts its fist on a point it can reach", "[editor][ik]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::Torso, -15.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose);
    const Vec2 Shoulder = getJointPivot(getRig(), Body, Upper);
    for (const Vec2 Offset : {Vec2{0.3f, -0.2f}, Vec2{0.35f, 0.25f}, Vec2{0.1f, 0.4f}}) {
        const Vec2 Target = Shoulder + Offset;
        const ArmSolution Solution = solveArmIk(getRig(), Body, Forearm, Target, ElbowBend::Down);
        CHECK(Solution.Reached);
        CHECK(Solution.Bend == ElbowBend::Down);
        CHECK(Solution.Angles.Forearm > 0.0f);
        checkLimits(getRig(), Solution.Angles);
        const Vec2 Fist = poseFist(getRig(), Pose, Solution.Angles);
        CHECK((Fist - Target).getLength() < 2e-3f);
        CHECK((Solution.Fist - Fist).getLength() < 1e-4f);
    }
}

TEST_CASE("The elbow bends either way when the joint allows it", "[editor][ik]") {
    const rig::RigDef& Def = getDoubleJointedRig();
    const PosedBody Body = poseBody(Def, anim::Pose{});
    const Vec2 Shoulder = getJointPivot(Def, Body, Upper);
    const Vec2 Target = Shoulder + Vec2{0.35f, -0.1f};
    const ArmSolution Down = solveArmIk(Def, Body, Forearm, Target, ElbowBend::Down);
    const ArmSolution Up = solveArmIk(Def, Body, Forearm, Target, ElbowBend::Up);
    REQUIRE(Down.Reached);
    REQUIRE(Up.Reached);
    CHECK(Down.Bend == ElbowBend::Down);
    CHECK(Up.Bend == ElbowBend::Up);
    CHECK(Down.Angles.Forearm > 0.0f);
    CHECK(Up.Angles.Forearm < 0.0f);
    // The elbows are on either side of the line from the shoulder to the fist:
    // Down below it, Up above it.
    const PosedBody DownBody = [&] {
        anim::Pose Pose;
        Pose.setAngle(Upper, Down.Angles.Upper);
        Pose.setAngle(Forearm, Down.Angles.Forearm);
        return poseBody(Def, Pose);
    }();
    const PosedBody UpBody = [&] {
        anim::Pose Pose;
        Pose.setAngle(Upper, Up.Angles.Upper);
        Pose.setAngle(Forearm, Up.Angles.Forearm);
        return poseBody(Def, Pose);
    }();
    const Vec2 Line = Target - Shoulder;
    CHECK(cross(Line, getJointPivot(Def, DownBody, Forearm) - Shoulder) < 0.0f);
    CHECK(cross(Line, getJointPivot(Def, UpBody, Forearm) - Shoulder) > 0.0f);
    CHECK((poseFist(Def, anim::Pose{}, Up.Angles) - Target).getLength() < 2e-3f);
}

TEST_CASE("A bend past the elbow limit falls back to the other bend", "[editor][ik]") {
    // The humanoid's elbow bends 0..150 degrees: only Down.
    const PosedBody Body = poseBody(getRig(), anim::Pose{});
    const Vec2 Target = getJointPivot(getRig(), Body, Upper) + Vec2{0.35f, -0.1f};
    const ArmSolution Solution = solveArmIk(getRig(), Body, Forearm, Target, ElbowBend::Up);
    CHECK(Solution.Reached);
    CHECK(Solution.Bend == ElbowBend::Down);
    checkLimits(getRig(), Solution.Angles);
}

TEST_CASE("An unreachable target stretches the arm towards it", "[editor][ik]") {
    const PosedBody Body = poseBody(getRig(), anim::Pose{});
    const Vec2 Shoulder = getJointPivot(getRig(), Body, Upper);
    const Vec2 Target = Shoulder + Vec2{2.0f, 0.5f};
    const ArmSolution Solution = solveArmIk(getRig(), Body, Forearm, Target, ElbowBend::Down);
    CHECK_FALSE(Solution.Reached);
    CHECK(Solution.Angles.Forearm == Approx(0.0f).margin(1e-4));
    checkLimits(getRig(), Solution.Angles);
    const Vec2 Fist = poseFist(getRig(), anim::Pose{}, Solution.Angles);
    // Straight (0.26 + 0.29 m) and on the line to the target.
    CHECK((Fist - Shoulder).getLength() == Approx(0.55f).margin(1e-3));
    CHECK(std::abs(cross((Fist - Shoulder).getNormalized(), (Target - Shoulder).getNormalized())) < 1e-3f);
}

TEST_CASE("The solved arm stays within the limits of the rig", "[editor][ik]") {
    anim::Pose Pose;
    Pose.setAngle(BodyPart::Torso, 20.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose);
    const Vec2 Shoulder = getJointPivot(getRig(), Body, Upper);
    // Targets all round the shoulder, near and far: some only reachable
    // past the limits (low behind the back: the shoulder turns back 60 degrees).
    for (int Step = 0; Step < 24; ++Step) {
        for (const float Distance : {0.1f, 0.3f, 0.5f, 0.8f}) {
            const Vec2 Target = Shoulder + rotate({Distance, 0.0f}, static_cast<float>(Step) * 15.0f * RadiansPerDegree);
            for (const ElbowBend Bend : {ElbowBend::Down, ElbowBend::Up}) {
                const ArmSolution Solution = solveArmIk(getRig(), Body, Forearm, Target, Bend);
                checkLimits(getRig(), Solution.Angles);
                const Vec2 Fist = poseFist(getRig(), Pose, Solution.Angles);
                CHECK((Solution.Fist - Fist).getLength() < 1e-4f);
                if (Solution.Reached) CHECK((Fist - Target).getLength() < 2e-3f);
            }
        }
    }
    const ArmSolution Behind =
        solveArmIk(getRig(), Body, Forearm, Shoulder + Vec2{-0.3f, -0.4f}, ElbowBend::Down);
    CHECK_FALSE(Behind.Reached);
}

TEST_CASE("The wrist points the blade at the target", "[editor][ik]") {
    const std::array Held = {makeSword()};
    anim::Pose Pose;
    Pose.setAngle(Upper, 60.0f * RadiansPerDegree);
    Pose.setAngle(Forearm, 30.0f * RadiansPerDegree);
    Pose.setWeaponAngle(10.0f * RadiansPerDegree);
    const PosedBody Body = poseBody(getRig(), Pose, Held);
    const PosedWeapon& Weapon = Body.Weapons.front();
    auto checkAim = [&](const WeaponAim& Aim, Vec2 Target) {
        anim::Pose Aimed = Pose;
        Aimed.setAngle(Upper, Aim.Arm.Upper);
        Aimed.setAngle(Forearm, Aim.Arm.Forearm);
        Aimed.setWeaponAngle(Aim.Wrist);
        const PosedWeapon After = poseBody(getRig(), Aimed, Held).Weapons.front();
        const Vec2 Blade = (After.Tip - After.Fist).getNormalized();
        CHECK(cross(Blade, (Target - Weapon.Fist).getNormalized()) == Approx(0.0f).margin(1e-4));
        CHECK(dot(Blade, (Target - Weapon.Fist).getNormalized()) > 0.0f);
        CHECK(isInRange(Aim.Wrist, getWristRange(getRig())));
        checkLimits(getRig(), Aim.Arm);
    };

    SECTION("within the wrist: only the wrist turns, the fist stays") {
        const Vec2 Target = Weapon.Fist + rotate(Weapon.Tip - Weapon.Fist, 30.0f * RadiansPerDegree);
        const WeaponAim Aim = aimWeapon(getRig(), Body, Weapon, Target);
        CHECK(Aim.Wrist == Approx(40.0f * RadiansPerDegree));
        CHECK(Aim.Arm.Upper == Approx(60.0f * RadiansPerDegree));
        CHECK(Aim.Arm.Forearm == Approx(30.0f * RadiansPerDegree));
        checkAim(Aim, Target);
    }
    SECTION("past the wrist limit: the elbow takes the rest") {
        const Vec2 Target = Weapon.Fist + rotate(Weapon.Tip - Weapon.Fist, 150.0f * RadiansPerDegree);
        const WeaponAim Aim = aimWeapon(getRig(), Body, Weapon, Target);
        CHECK(Aim.Wrist == Approx(getWristRange(getRig()).Upper));
        CHECK(Aim.Arm.Forearm == Approx(70.0f * RadiansPerDegree));
        CHECK(Aim.Arm.Upper == Approx(60.0f * RadiansPerDegree));
        checkAim(Aim, Target);
    }
}

TEST_CASE("The grip point lies along the weapon from the fist", "[editor][ik]") {
    const PosedWeapon Weapon = {.Fist = {1.0f, 1.0f}, .Tip = {1.0f, 2.0f}};
    CHECK(getGripPoint(Weapon, 0.1f).Y == Approx(1.1f));
    CHECK(getGripPoint(Weapon, -0.17f).Y == Approx(0.83f));
    CHECK(getGripPoint(Weapon, 3.0f).Y == Approx(2.0f));   // no farther than the tip
    CHECK(getGripPoint(Weapon, 0.1f).X == Approx(1.0f));
}

TEST_CASE("Dragging a part turns its joint about the pivot", "[editor][ik]") {
    const AngleRange Range = {.Lower = -1.0f, .Upper = 1.0f};
    const Vec2 Pivot = {1.0f, 1.0f};
    // A quarter turn counter-clockwise, from below the pivot to its right.
    CHECK(dragJointAngle(0.2f, Pivot, {1.0f, 0.0f}, {1.5f, 1.0f}, {.Lower = -2.0f, .Upper = 2.0f}) ==
          Approx(0.2f + std::numbers::pi_v<float> * 0.5f));
    CHECK(dragJointAngle(0.2f, Pivot, {1.0f, 0.0f}, {1.5f, 1.0f}, Range) == Approx(1.0f));
    CHECK(dragJointAngle(0.2f, Pivot, {1.0f, 0.0f}, {0.5f, 1.0f}, Range) == Approx(-1.0f));
}
