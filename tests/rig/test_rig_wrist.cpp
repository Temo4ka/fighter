#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <optional>
#include <ranges>
#include <vector>

#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
constexpr float SwordMassKg = 1.2f;

RigDef loadHumanoid() { return loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

/// A fighter of base stats facing \p FacingRight; the forearm of \p Held
/// items weighs their masses more, as the profile counts them.
RigSetup makeSetup(bool FacingRight, std::vector<HeldItem> Held = {}) {
    const stats::PhysicalProfile Profile = stats::computeProfile({}, {}, stats::BalanceTable::getDefaults());
    RigSetup Setup;
    Setup.Origin = {0.0f, 0.005f};
    Setup.FacingRight = FacingRight;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    for (const HeldItem& Item : Held) Setup.MassKg[static_cast<size_t>(Item.Part)] += Item.WeaponMassKg;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    Setup.Held = std::move(Held);
    return Setup;
}

HeldItem makeSword(std::optional<float> AngleDeg = std::nullopt) {
    return {.Part = BodyPart::ForearmL, .WeaponReachM = 0.55f, .WeaponAngleDeg = AngleDeg, .WeaponMassKg = SwordMassKg};
}

/// The left arm reaching forward, the elbow straight.
PerBodyPart<float> makeReach() {
    PerBodyPart<float> Angles{};
    Angles[static_cast<size_t>(BodyPart::UpperArmL)] = 1.57f;
    return Angles;
}

/// A rig standing on a floor in the reaching pose.
struct Stage {
    physics::World PhysWorld;
    Rig Body;

    explicit Stage(const RigSetup& Setup) : Body(PhysWorld, loadHumanoid(), Setup) {
        const physics::Body Ground = PhysWorld.createBody({.Type = physics::BodyType::Static});
        PhysWorld.addShape(Ground,
                           {.Kind = physics::ShapeKind::Box, .Center = {0.0f, -0.5f}, .HalfExtents = {10.0f, 0.5f}});
        Body.setTargetAngles(makeReach());
        Body.snapToTargets();
    }

    void run(int Steps) {
        for (int Step = 0; Step < Steps; ++Step) {
            Body.planMotion(Dt);
            Body.applyControl(Dt);
            PhysWorld.step(Dt);
        }
    }
};

} // namespace

TEST_CASE("Rig: a weapon is a body on the wrist that weighs the item, taken off the forearm", "[rig][wrist]") {
    physics::World Bare;
    const Rig Unarmed(Bare, loadHumanoid(), makeSetup(true));
    physics::World Armed;
    const RigSetup Setup = makeSetup(true, {makeSword()});
    const Rig Swordsman(Armed, loadHumanoid(), Setup);

    CHECK(Armed.getBodyCount() == Bare.getBodyCount() + 1);
    CHECK(Armed.getJointCount() == Bare.getJointCount() + 1);
    // The whole fighter weighs the same: the sword's mass moved to its body.
    CHECK(Swordsman.getTotalMass() == Approx(Unarmed.getTotalMass() + SwordMassKg));
    // The fist and the weapon strike with the arm and the weapon.
    const float ArmMass = Setup.MassKg[static_cast<size_t>(BodyPart::ForearmL)] +
                          Setup.MassKg[static_cast<size_t>(BodyPart::UpperArmL)];
    CHECK(Swordsman.getStrikeMass(BodyPart::ForearmL) == Approx(ArmMass));
    CHECK(Swordsman.getDefaultWristAngle() == 0.0f);
    CHECK_FALSE(Unarmed.getDefaultWristAngle());
    CHECK_FALSE(Unarmed.getWristAngle(BodyPart::ForearmL));
}

TEST_CASE("Rig: the wrist follows its target, else the item's default, within its limits", "[rig][wrist]") {
    const RigDef Def = loadHumanoid();
    for (const bool FacingRight : {true, false}) {
        CAPTURE(FacingRight);
        Stage Scene(makeSetup(FacingRight, {makeSword(30.0f)}));
        Rig& Body = Scene.Body;
        // No clip sets the wrist: the item's angle.
        Scene.run(60);
        CHECK(*Body.getWristTarget(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree));
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree).margin(0.05f));

        // A clip's wrist (as for facing right), followed like a joint.
        Body.setWristAngle(-1.0f);
        Scene.run(60);
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(-1.0f).margin(0.05f));
        // The weapon turned with it: the arm reaches forward, the blade
        // points down and forward from the fist.
        const Vec2 Fist = Body.getPartPosition(BodyPart::ForearmL);
        const Vec2 Tip = *Body.getWeaponTip(BodyPart::ForearmL);
        CHECK(Tip.Y < Fist.Y - 0.3f);
        CHECK((Tip.X - Fist.X) * (FacingRight ? 1.0f : -1.0f) > 0.2f);

        // Beyond the wrist's limits: the limit.
        Body.setWristAngle(3.0f);
        CHECK(*Body.getWristTarget(BodyPart::ForearmL) == Approx(Def.Weapon.WristUpperAngle));
        // Cleared: back to the item's default.
        Body.setWristAngle(std::nullopt);
        Scene.run(60);
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree).margin(0.05f));
    }
}

TEST_CASE("Rig: a turn mirrors the weapon with its wrist", "[rig][wrist]") {
    Stage Scene(makeSetup(true, {makeSword()}));
    Rig& Body = Scene.Body;
    Body.setWristAngle(0.6f);
    Scene.run(60);
    const float Before = *Body.getWristAngle(BodyPart::ForearmL);
    Body.setFacing(false);
    Scene.run(2);
    CHECK_FALSE(Body.isFacingRight());
    CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(Before).margin(0.05f));
    // The tip is in front, to the left now.
    CHECK(Body.getWeaponTip(BodyPart::ForearmL)->X < Body.getPartPosition(BodyPart::Pelvis).X);
}

TEST_CASE("Rig: the other hand grips the handle of a turned two-handed weapon", "[rig][wrist]") {
    HeldItem Great{.Part = BodyPart::ForearmL, .WeaponReachM = 0.95f, .WeaponMassKg = 3.0f};
    RigSetup Setup = makeSetup(true, {Great});
    Setup.GripPart = BodyPart::ForearmR;
    PerBodyPart<float> Both = makeReach();
    Both[static_cast<size_t>(BodyPart::UpperArmR)] = 1.57f;
    Stage Scene(Setup);
    Scene.Body.setTargetAngles(Both);
    Scene.Body.setWristAngle(0.0f);
    Scene.run(90);
    REQUIRE(Scene.Body.getGripPart() == BodyPart::ForearmR);
    const float Straight = Scene.Body.getGripGap();
    Scene.Body.setWristAngle(0.5f);
    Scene.run(90);
    // The spring holds the fist on the handle of the weapon body wherever
    // the wrist turns it: no further off than along the forearm's axis.
    const float Turned = Scene.Body.getGripGap();
    CHECK(Turned < Straight + 0.02f);
    CHECK(Turned <= Scene.Body.getControl().GripMaxStretch + 0.005f);
    CHECK(*Scene.Body.getWristAngle(BodyPart::ForearmL) == Approx(0.5f).margin(0.1f));
}

TEST_CASE("Rig: weapon transforms run from the fist to the tip along -Y", "[rig][wrist]") {
    Stage Scene(makeSetup(true, {makeSword()}));
    Scene.Body.setWristAngle(0.4f);
    Scene.run(30);
    std::vector<PartTransform> Weapons;
    Scene.Body.getWeaponTransforms(Weapons);
    REQUIRE(Weapons.size() == 1);
    const PartTransform& Blade = Weapons.front();
    CHECK(Blade.Part == BodyPart::ForearmL);
    const Vec2 Down{std::sin(Blade.Angle), -std::cos(Blade.Angle)};
    const Vec2 End = Blade.Position + Down * (Blade.Size.Y * 0.5f);
    const Vec2 Tip = *Scene.Body.getWeaponTip(BodyPart::ForearmL);
    CHECK(End.X == Approx(Tip.X).margin(1e-3f));
    CHECK(End.Y == Approx(Tip.Y).margin(1e-3f));
    // As wide as the mount's weapon.
    CHECK(Blade.Size.X == Approx(loadHumanoid().Weapon.Width));
}

TEST_CASE("Rig: a weapon in each hand makes two weapon bodies on two wrists", "[rig][wrist]") {
    physics::World Bare;
    const Rig Unarmed(Bare, loadHumanoid(), makeSetup(true));
    HeldItem Off = makeSword(-10.0f);
    Off.Part = BodyPart::ForearmR;
    physics::World Armed;
    const Rig Duelist(Armed, loadHumanoid(), makeSetup(true, {makeSword(30.0f), Off}));
    CHECK(Armed.getBodyCount() == Bare.getBodyCount() + 2);
    CHECK(Armed.getJointCount() == Bare.getJointCount() + 2);
    CHECK(Duelist.getTotalMass() == Approx(Unarmed.getTotalMass() + 2.0f * SwordMassKg));
    // Each weapon has its own default; the first is the main hand's.
    CHECK(*Duelist.getDefaultWristAngle(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree));
    CHECK(*Duelist.getDefaultWristAngle(BodyPart::ForearmR) == Approx(-10.0f * RadiansPerDegree));
    CHECK(*Duelist.getDefaultWristAngle() == Approx(30.0f * RadiansPerDegree));
    CHECK_FALSE(Unarmed.getDefaultWristAngle(BodyPart::ForearmL));
    std::vector<PartTransform> Weapons;
    Duelist.getWeaponTransforms(Weapons);
    REQUIRE(Weapons.size() == 2);
    CHECK(Weapons[0].Part == BodyPart::ForearmL);
    CHECK(Weapons[1].Part == BodyPart::ForearmR);
    CHECK_FALSE(Duelist.getGripPart());
}

TEST_CASE("Rig: each wrist follows its own target, else its own item's default", "[rig][wrist]") {
    HeldItem Off = makeSword(-10.0f);
    Off.Part = BodyPart::ForearmR;
    for (const bool FacingRight : {true, false}) {
        CAPTURE(FacingRight);
        Stage Scene(makeSetup(FacingRight, {makeSword(30.0f), Off}));
        Rig& Body = Scene.Body;
        Scene.run(60);
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree).margin(0.05f));
        CHECK(*Body.getWristAngle(BodyPart::ForearmR) == Approx(-10.0f * RadiansPerDegree).margin(0.05f));

        // One hand's wrist: the other keeps its own.
        Body.setWristAngle(BodyPart::ForearmR, 0.8f);
        Scene.run(60);
        CHECK(*Body.getWristAngle(BodyPart::ForearmR) == Approx(0.8f).margin(0.05f));
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(30.0f * RadiansPerDegree).margin(0.05f));
        Body.setWristAngle(BodyPart::ForearmL, -0.5f);
        Scene.run(60);
        CHECK(*Body.getWristAngle(BodyPart::ForearmL) == Approx(-0.5f).margin(0.05f));
        CHECK(*Body.getWristAngle(BodyPart::ForearmR) == Approx(0.8f).margin(0.05f));

        // Cleared one by one: back to that weapon's default.
        Body.setWristAngle(BodyPart::ForearmR, std::nullopt);
        CHECK(*Body.getWristTarget(BodyPart::ForearmR) == Approx(-10.0f * RadiansPerDegree));
        CHECK(*Body.getWristTarget(BodyPart::ForearmL) == Approx(-0.5f));
        // Both at once, as with one weapon.
        Body.setWristAngle(0.2f);
        CHECK(*Body.getWristTarget(BodyPart::ForearmL) == Approx(0.2f));
        CHECK(*Body.getWristTarget(BodyPart::ForearmR) == Approx(0.2f));
        // A hand without a weapon: nothing.
        Body.setWristAngle(BodyPart::Head, 1.0f);
        CHECK_FALSE(Body.getWristTarget(BodyPart::Head));
    }
}
