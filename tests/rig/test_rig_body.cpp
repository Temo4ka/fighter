#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <ranges>
#include <vector>

#include "anim/clip.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "rig/spacing.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

// The body of task 2.1 without a battle: feet, crouch, turning around,
// knockdown direction, staying down, the weapon, limbs stuck in the
// opponent, walls and the spacing of two fighters (rig/spacing.hpp).

namespace {

constexpr float Dt = 1.0f / 60.0f;
constexpr float ArenaHalfWidth = 3.0f;

RigDef loadHumanoid() { return loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

PerBodyPart<float> loadStance() {
    const anim::Clip Stance = anim::loadClip(std::filesystem::path(FIGHTER_DATA_DIR) / "poses" / "stance.json");
    return anim::sampleClip(Stance, 0.0f).Angles;
}

RigSetup makeSetup(float X, bool FacingRight, uint8_t Index = 0, float MassScale = 1.0f) {
    const stats::PhysicalProfile Profile = stats::computeProfile({}, {}, stats::BalanceTable::getDefaults());
    RigSetup Setup;
    Setup.Origin = {X, 0.005f};
    Setup.FacingRight = FacingRight;
    Setup.FighterIndex = Index;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg * MassScale;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    return Setup;
}

/// A floor and two walls with inner faces at +-ArenaHalfWidth.
void addArena(physics::World& PhysWorld) {
    const physics::Body Ground = PhysWorld.createBody({.Type = physics::BodyType::Static});
    PhysWorld.addShape(Ground,
                       {.Kind = physics::ShapeKind::Box, .Center = {0.0f, -0.5f}, .HalfExtents = {10.0f, 0.5f}});
    for (const auto& Side : {-1.0f, 1.0f}) {
        PhysWorld.addShape(Ground, {.Kind = physics::ShapeKind::Box,
                                    .Center = {Side * (ArenaHalfWidth + 0.1f), 2.0f},
                                    .HalfExtents = {0.1f, 2.0f}});
    }
}

/// One fighter in the stance on the floor of an arena.
struct Solo {
    physics::World PhysWorld;
    Rig Body;

    explicit Solo(const RigSetup& Setup, const PerBodyPart<float>& Targets = loadStance())
        : Body(PhysWorld, loadHumanoid(), Setup) {
        addArena(PhysWorld);
        Body.setTargetAngles(Targets);
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

/// Two fighters facing each other, kept apart like in a battle.
struct Duel {
    physics::World PhysWorld;
    Rig Left;
    Rig Right;
    SpacingParams Spacing{.ArenaHalfWidth = ArenaHalfWidth};

    Duel(float LeftX, float RightX, float RightMassScale = 1.0f)
        : Left(PhysWorld, loadHumanoid(), makeSetup(LeftX, true, 0)),
          Right(PhysWorld, loadHumanoid(), makeSetup(RightX, false, 1, RightMassScale)) {
        addArena(PhysWorld);
        for (Rig* Body : {&Left, &Right}) {
            Body->setTargetAngles(loadStance());
            Body->snapToTargets();
        }
    }

    void run(int Steps) {
        for (int Step = 0; Step < Steps; ++Step) {
            Left.planMotion(Dt);
            Right.planMotion(Dt);
            keepApart(Left, Right, Spacing, Dt);
            Left.applyControl(Dt);
            Right.applyControl(Dt);
            PhysWorld.step(Dt);
        }
    }
};

float getPelvisX(const Rig& Body) { return Body.getPartPosition(BodyPart::Pelvis).X; }

} // namespace

TEST_CASE("Rig: bent knees lower the pelvis and the feet stay on the floor", "[rig]") {
    Solo Standing(makeSetup(0.0f, true));
    PerBodyPart<float> Crouch = loadStance();
    for (const auto Part : {BodyPart::ThighL, BodyPart::ThighR}) Crouch[static_cast<size_t>(Part)] += 0.6f;
    for (const auto Part : {BodyPart::ShinL, BodyPart::ShinR}) Crouch[static_cast<size_t>(Part)] -= 1.2f;
    for (const auto Part : {BodyPart::FootL, BodyPart::FootR}) Crouch[static_cast<size_t>(Part)] += 0.6f;
    Solo Crouching(makeSetup(0.0f, true), Crouch);
    Standing.run(30);
    Crouching.run(30);

    CHECK(Crouching.Body.getPartPosition(BodyPart::Pelvis).Y <
          Standing.Body.getPartPosition(BodyPart::Pelvis).Y - 0.1f);
    CHECK(Crouching.Body.getFloorPoint().Y == Approx(0.0f).margin(0.005f));
    // Getting into the crouch from the stance lowers the pelvis smoothly.
    Standing.Body.setTargetAngles(Crouch);
    Standing.run(30);
    CHECK(Standing.Body.getPartPosition(BodyPart::Pelvis).Y ==
          Approx(Crouching.Body.getPartPosition(BodyPart::Pelvis).Y).margin(0.01f));
    CHECK(Standing.Body.getFloorPoint().Y == Approx(0.0f).margin(0.005f));
}

TEST_CASE("Rig: a planted foot holds its place while the pelvis moves", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    Rig& Body = Stage.Body;
    const ControlParams& Control = Body.getControl();
    Stage.run(10);
    REQUIRE(Body.isFootLocked(BodyPart::FootL));
    REQUIRE(Body.isFootLocked(BodyPart::FootR));
    const float FootX = Body.getPartPosition(BodyPart::FootL).X;

    // A push forwards shorter than the slip: the front knee bends more, the
    // front foot stays. (The straight rear leg cannot reach that far: its
    // foot is dragged.)
    Body.addPush(Control.FootLockSlip * 0.5f);
    Stage.run(60);
    CHECK(getPelvisX(Body) > Control.FootLockSlip * 0.3f);
    CHECK(Body.getPartPosition(BodyPart::FootL).X == Approx(FootX).margin(0.002f));
    CHECK(Body.getFloorPoint().Y == Approx(0.0f).margin(0.01f));

    // A longer one drags it along.
    Body.addPush(0.5f);
    Stage.run(90);
    CHECK(Body.getPartPosition(BodyPart::FootL).X > FootX + 0.2f);
    CHECK(Body.isFootLocked(BodyPart::FootL));
}

TEST_CASE("Rig: setFacing turns the fighter around without a jolt", "[rig]") {
    Solo Turning(makeSetup(0.5f, true));
    Solo Left(makeSetup(0.5f, false));
    Turning.run(30);
    Left.run(30);
    const Vec2 HeadBefore = Turning.Body.getPartPosition(BodyPart::Head);

    Turning.Body.setFacing(false);
    CHECK(Turning.Body.isTurnPending());
    CHECK(Turning.Body.isFacingRight());
    Turning.run(1);
    CHECK_FALSE(Turning.Body.isTurnPending());
    CHECK_FALSE(Turning.Body.isFacingRight());
    // Mirrored about the pelvis: the head was in front, it still is.
    CHECK(Turning.Body.getPartPosition(BodyPart::Head).X == Approx(1.0f - HeadBefore.X).margin(0.02f));

    // Nothing flies apart, and it settles into the same pose as a fighter
    // that faced left from the start.
    for (int Step = 0; Step < 60; ++Step) {
        const Vec2 Before = Turning.Body.getPartPosition(BodyPart::ForearmL);
        Turning.run(1);
        CHECK((Turning.Body.getPartPosition(BodyPart::ForearmL) - Before).getLength() / Dt < 1.0f);
    }
    for (const auto Part : {BodyPart::Head, BodyPart::ForearmL, BodyPart::FootL, BodyPart::FootR}) {
        CHECK(Turning.Body.getPartPosition(Part).X == Approx(Left.Body.getPartPosition(Part).X).margin(0.02f));
        CHECK(Turning.Body.getPartPosition(Part).Y == Approx(Left.Body.getPartPosition(Part).Y).margin(0.02f));
    }
    CHECK(Turning.Body.getJointAngle(BodyPart::ForearmL) ==
          Approx(Left.Body.getJointAngle(BodyPart::ForearmL)).margin(0.05f));

    // A fighter on the floor turns only when it stands again.
    Turning.Body.applyHit(1.0f, -1.0f, true);
    Turning.Body.setFacing(true);
    Turning.run(5);
    CHECK(Turning.Body.isTurnPending());
}

TEST_CASE("Rig: a knockdown falls the way the hit pushed and spins by where it landed", "[rig]") {
    constexpr float Impulse = 60.0f;
    Solo High(makeSetup(0.0f, false));
    Solo Low(makeSetup(0.0f, false));
    High.run(10);
    Low.run(10);
    const Vec2 Head = High.Body.getPartPosition(BodyPart::Head);
    const Vec2 Shin = Low.Body.getPartPosition(BodyPart::ShinL);

    // Pushed to the right: at the head the body topples backwards (turns
    // clockwise); at the shin the legs are swept (counter-clockwise).
    High.Body.applyHit(Impulse, {1.0f, 0.0f}, Head, true);
    Low.Body.applyHit(Impulse, {1.0f, 0.0f}, Shin, true);
    REQUIRE(High.Body.getPosture() == Posture::KnockedDown);
    REQUIRE(Low.Body.getPosture() == Posture::KnockedDown);
    High.run(5);
    Low.run(5);
    CHECK(High.Body.getPartPosition(BodyPart::Torso).X > 0.0f);
    CHECK(High.Body.getPartPosition(BodyPart::Head).X > Head.X);
    CHECK(Low.Body.getPartPosition(BodyPart::ShinL).X > Shin.X);

    High.run(60);
    Low.run(60);
    // The high hit throws the head the way of the push, the low hit leaves
    // it behind the feet.
    const auto getLean = [](const Rig& Body) {
        return Body.getPartPosition(BodyPart::Head).X - Body.getPartPosition(BodyPart::FootL).X;
    };
    CHECK(getLean(High.Body) > getLean(Low.Body) + 0.3f);
    CHECK(High.Body.getPartPosition(BodyPart::Head).Y < 0.7f);

    // A push into the floor drives the body down, not sideways.
    Solo Down(makeSetup(0.0f, true));
    Down.run(10);
    Down.Body.applyHit(Impulse, {0.0f, -1.0f}, Down.Body.getCenterOfMass(), true);
    Down.run(1);
    CHECK(std::abs(Down.Body.getPartPosition(BodyPart::Torso).X - Down.Body.getPartPosition(BodyPart::Pelvis).X) <
          0.2f);
}

TEST_CASE("Rig: a fighter told to stay down does not get up", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    Rig& Body = Stage.Body;
    const ControlParams& Control = Body.getControl();
    Stage.run(10);

    // A knockout drops a standing fighter where it is.
    Body.setStayDown(true);
    CHECK(Body.getPosture() == Posture::KnockedDown);
    CHECK(Body.isStayingDown());
    Stage.run(static_cast<int>((Control.KnockdownSec + Control.GetUpSec + 1.0f) / Dt));
    CHECK(Body.getPosture() == Posture::KnockedDown);
    CHECK(Body.getPartPosition(BodyPart::Head).Y < 0.7f);
    CHECK(Body.getStiffness() == Approx(Control.KnockoutStiffness));

    Body.setStayDown(false);
    Stage.run(2);
    CHECK(Body.getPosture() == Posture::GettingUp);
}

TEST_CASE("Rig: the weapon extends the forearm that holds it", "[rig]") {
    // The right arm straight forward: the forearm is level with the shoulder.
    PerBodyPart<float> Reaching = loadStance();
    Reaching[static_cast<size_t>(BodyPart::UpperArmR)] = 1.57f;
    Reaching[static_cast<size_t>(BodyPart::ForearmR)] = 0.0f;
    RigSetup Armed = makeSetup(0.0f, true);
    Armed.WeaponReachM = 0.5f;
    Solo Bare(makeSetup(0.0f, true), Reaching);
    Solo Sword(Armed, Reaching);
    Bare.run(60);
    Sword.run(60);

    CHECK(Bare.Body.getWeaponReach() == 0.0f);
    CHECK(Sword.Body.getWeaponReach() == 0.5f);
    CHECK(Sword.Body.getExtentX().Max == Approx(Bare.Body.getExtentX().Max + 0.5f).margin(0.03f));
    // The forearm weighs what the profile says, weapon included.
    CHECK(Sword.Body.getTotalMass() == Approx(Bare.Body.getTotalMass()));

    // The blade is a part of the forearm: what it hits is hit by ForearmR.
    physics::Body Target = Sword.PhysWorld.createBody({
        .Position = {Sword.Body.getExtentX().Max + 0.2f, Sword.Body.getPartPosition(BodyPart::ForearmR).Y},
        .Part = physics::PartRef{1, BodyPart::Head},
    });
    Sword.PhysWorld.addShape(Target, {.Kind = physics::ShapeKind::Circle, .Radius = 0.1f, .CollisionGroup = -2,
                                      .EnableHitEvents = true});
    Target.setMass(5.0f);
    Target.setLinearVelocity({-4.0f, 0.0f});
    std::vector<physics::HitEvent> Hits;
    for (int Step = 0; Step < 10; ++Step) {
        Sword.run(1);
        std::ranges::copy(Sword.PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
    REQUIRE_FALSE(Hits.empty());
    const bool ByForearm = Hits[0].Attacker.Part == BodyPart::ForearmR || Hits[0].Victim.Part == BodyPart::ForearmR;
    CHECK(ByForearm);
    CHECK(Hits[0].Point.X > Bare.Body.getExtentX().Max);
}

TEST_CASE("Rig: the arms of two fighters pass each other", "[rig]") {
    // Close enough for the guards to overlap, too far for a fist to reach
    // the other chest.
    Duel Close(-0.33f, 0.33f);
    Close.run(30);
    for (const auto Part : {BodyPart::ForearmL, BodyPart::UpperArmL}) {
        CHECK(Close.Left.getJointAngle(Part) == Approx(loadStance()[static_cast<size_t>(Part)]).margin(0.03f));
        CHECK(Close.Right.getJointAngle(Part) == Approx(loadStance()[static_cast<size_t>(Part)]).margin(0.03f));
    }
}

TEST_CASE("Rig: a limb stuck in the opponent lets go until it is free", "[rig]") {
    // The left fighter reaches straight into the right one's chest.
    Duel Stuck(-0.3f, 0.3f, 3.0f);
    PerBodyPart<float> Reaching = loadStance();
    Reaching[static_cast<size_t>(BodyPart::UpperArmL)] = 1.57f;
    Reaching[static_cast<size_t>(BodyPart::ForearmL)] = 0.0f;
    Stuck.Left.setTargetAngles(Reaching);
    const ControlParams& Control = Stuck.Left.getControl();

    bool Freed = false;
    for (int Step = 0; Step < 60 && !Freed; ++Step) {
        Stuck.run(1);
        Freed = Stuck.Left.isUnjamming(BodyPart::ForearmL);
    }
    REQUIRE(Freed);
    CHECK(Stuck.Left.isUnjamming(BodyPart::UpperArmL));   // the whole arm
    CHECK_FALSE(Stuck.Left.isUnjamming(BodyPart::ForearmR));
    // Free, it reaches its target through the opponent.
    Stuck.run(30);
    CHECK(Stuck.Left.getJointAngle(BodyPart::UpperArmL) == Approx(1.57f).margin(Control.JamAngle));

    // Pulled back out of the opponent, it collides again.
    Stuck.Left.setTargetAngles(loadStance());
    Stuck.Left.setMoveVelocity(-1.0f);
    Stuck.run(60);
    CHECK_FALSE(Stuck.Left.isUnjamming(BodyPart::ForearmL));
}

TEST_CASE("Rig: a fighter at the wall touches it, its back against it when facing away", "[rig]") {
    Solo Stage(makeSetup(0.0f, false));
    Rig& Body = Stage.Body;
    const float MaxX = 2.75f;
    Body.setMoveVelocity(5.0f);
    for (int Step = 0; Step < 120; ++Step) {
        Body.planMotion(Dt);
        Body.updateWallContact(-MaxX, MaxX, ArenaHalfWidth);
        Body.applyControl(Dt);
        Stage.PhysWorld.step(Dt);
    }
    CHECK(getPelvisX(Body) == Approx(MaxX));
    CHECK(Body.getWallSide() == 1);
    CHECK(Body.isAgainstWall());   // faces left, the wall is behind

    // Knockback into the wall stops there.
    Body.setMoveVelocity(0.0f);
    Body.applyHit(30.0f, 1.0f, false);
    Body.planMotion(Dt);
    Body.updateWallContact(-MaxX, MaxX, ArenaHalfWidth);
    CHECK(Body.getController().getKnockback() == 0.0f);
    CHECK(Body.getController().getPlannedX() == Approx(MaxX));

    // Facing the wall, it touches it but its back is free.
    Body.setFacing(true);
    Body.applyControl(Dt);
    CHECK(Body.getWallSide() == 1);
    CHECK_FALSE(Body.isAgainstWall());
}

TEST_CASE("Rig: knocked down against the wall it slumps along it", "[rig]") {
    Solo Stage(makeSetup(2.75f, false));
    Rig& Body = Stage.Body;
    Stage.run(10);
    Body.updateWallContact(-2.75f, 2.75f, ArenaHalfWidth);
    REQUIRE(Body.isAgainstWall());
    Body.applyHit(80.0f, {1.0f, 0.0f}, Body.getPartPosition(BodyPart::Head), true);

    float Fastest = 0.0f;
    for (int Step = 0; Step < 90; ++Step) {
        const Vec2 Before = Body.getPartPosition(BodyPart::Head);
        Stage.run(1);
        Fastest = std::max(Fastest, (Body.getPartPosition(BodyPart::Head) - Before).getLength() / Dt);
        Body.updateWallContact(-2.75f, 2.75f, ArenaHalfWidth);
    }
    // The wall took the push: no bounce, the body stays in the arena and
    // lies against the wall.
    CHECK(Fastest < 6.0f);
    CHECK(Body.getExtentX().Max < ArenaHalfWidth + 0.02f);
    CHECK(Body.getPartPosition(BodyPart::Head).Y < 1.2f);
    CHECK(Body.getWallSide() == 1);
}
