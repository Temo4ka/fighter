#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <numbers>
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

    Duel(float LeftX, float RightX, float RightMassScale = 1.0f, const RigDef& Def = loadHumanoid())
        : Left(PhysWorld, Def, makeSetup(LeftX, true, 0)),
          Right(PhysWorld, Def, makeSetup(RightX, false, 1, RightMassScale)) {
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

    // A small push forwards: the front knee bends more, the front foot
    // stays. (The straight rear leg cannot reach that far: its foot is
    // dragged.)
    Body.addPush(Control.FootRestepDistance * 0.6f);
    Stage.run(60);
    CHECK(getPelvisX(Body) > Control.FootRestepDistance * 0.4f);
    CHECK(Body.getPartPosition(BodyPart::FootL).X == Approx(FootX).margin(0.002f));
    CHECK(Body.getFloorPoint().Y == Approx(0.0f).margin(0.01f));

    // A longer one: the foot holds while the push lasts, then, standing
    // still, the fighter steps it back under the body.
    const float PelvisX = getPelvisX(Body);
    const float Push = (Control.FootRestepDistance + Control.FootLockSlip) * 0.5f;
    Body.addPush(Push);
    Stage.run(10);
    CHECK(Body.getPartPosition(BodyPart::FootL).X == Approx(FootX).margin(0.002f));
    Stage.run(90);
    const float Moved = getPelvisX(Body) - PelvisX;
    CHECK(Moved > Push * 0.8f);
    CHECK(Body.getPartPosition(BodyPart::FootL).X - FootX == Approx(getPelvisX(Body)).margin(0.01f));
    CHECK(Body.isFootLocked(BodyPart::FootL));
    CHECK(Body.isFootLocked(BodyPart::FootR));

    // A push longer than the slip drags the planted feet along.
    const float DraggedFrom = Body.getPartPosition(BodyPart::FootL).X;
    Body.addPush(0.6f);
    Stage.run(20);
    CHECK(Body.getPartPosition(BodyPart::FootL).X > DraggedFrom + 0.1f);
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
    // The shipped rig lists no part in "passThrough" (the arms collide, a
    // jab hits the guard); a rig that lists the arms lets them pass.
    RigDef Def = loadHumanoid();
    REQUIRE(Def.PassThrough.none());
    for (const auto Part : {BodyPart::UpperArmL, BodyPart::ForearmL, BodyPart::UpperArmR, BodyPart::ForearmR}) {
        Def.PassThrough.set(static_cast<size_t>(Part));
    }
    // Close enough for the guards to overlap, too far for a fist to reach
    // the other chest.
    Duel Close(-0.33f, 0.33f, 1.0f, Def);
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

TEST_CASE("Rig: a fighter at the wall touches it with its back when facing away", "[rig]") {
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

TEST_CASE("keepApart: standing pelvises stay apart and the heavier one gives way less", "[rig]") {
    // The right fighter is twice as heavy; both walk into each other.
    Duel Clash(-0.5f, 0.5f, 2.0f);
    const float MinGap = 2.0f * Clash.Spacing.BodyHalfWidth;
    Clash.Left.setMoveVelocity(1.0f);
    Clash.Right.setMoveVelocity(-1.0f);
    float SmallestGap = 10.0f;
    for (int Step = 0; Step < 120; ++Step) {
        Clash.run(1);
        SmallestGap = std::min(SmallestGap, getPelvisX(Clash.Right) - getPelvisX(Clash.Left));
    }
    CHECK(SmallestGap == Approx(MinGap).margin(1e-4f));
    // Both push equally hard: the light one is pushed back.
    CHECK(getPelvisX(Clash.Left) < -0.3f);

    // The order of the arguments does not matter: a fighter may stand on
    // the other side after getting up.
    Duel Swapped(-0.2f, 0.2f);
    Swapped.Right.setMoveVelocity(0.0f);
    for (int Step = 0; Step < 30; ++Step) {
        Swapped.Left.planMotion(Dt);
        Swapped.Right.planMotion(Dt);
        keepApart(Swapped.Right, Swapped.Left, Swapped.Spacing, Dt);
        Swapped.Left.applyControl(Dt);
        Swapped.Right.applyControl(Dt);
        Swapped.PhysWorld.step(Dt);
    }
    CHECK(getPelvisX(Swapped.Right) - getPelvisX(Swapped.Left) == Approx(MinGap).margin(1e-4f));
}

TEST_CASE("keepApart: the walls stop the pelvis and a fighter at a wall does not give way", "[rig]") {
    Duel Corner(1.5f, 2.6f);
    Corner.Left.setMoveVelocity(1.0f);
    Corner.run(180);
    const float MaxX = ArenaHalfWidth - Corner.Spacing.BodyHalfWidth;
    CHECK(getPelvisX(Corner.Right) == Approx(MaxX));
    const float MinGap = 2.0f * Corner.Spacing.BodyHalfWidth;
    CHECK(getPelvisX(Corner.Right) - getPelvisX(Corner.Left) == Approx(MinGap).margin(1e-4f));
    CHECK(Corner.Right.getWallSide() == 1);
    CHECK(Corner.Right.isAgainstWall());
    CHECK(Corner.Left.getWallSide() == 0);
}

TEST_CASE("keepApart: a standing fighter does not walk through a lying one", "[rig]") {
    Duel Fallen(-1.0f, 0.2f);
    Fallen.run(10);
    Fallen.Right.applyHit(100.0f, {1.0f, 0.0f}, Fallen.Right.getPartPosition(BodyPart::Head), true);
    Fallen.run(60);
    REQUIRE(Fallen.Right.getPosture() == Posture::KnockedDown);

    Fallen.Left.setMoveVelocity(1.0f);
    for (int Step = 0; Step < 60; ++Step) {
        Fallen.run(1);
        const float Body = Fallen.Right.getExtentX().Min;
        CHECK(getPelvisX(Fallen.Left) <=
              Approx(Body - Fallen.Spacing.BodyHalfWidth - Fallen.Left.getControl().LyingClearance).margin(0.01f));
    }
}

TEST_CASE("pushApartOnHit: a hit at close range pushes the fighters apart", "[rig]") {
    Duel Close(-0.26f, 0.26f, 2.0f);
    Close.run(5);
    const float CloseRange = Close.Right.getControl().CloseRange;
    const float Deficit = CloseRange - (getPelvisX(Close.Right) - getPelvisX(Close.Left));
    REQUIRE(Deficit > 0.1f);
    CHECK(pushApartOnHit(Close.Left, Close.Right) == Approx(Deficit));
    // Split by mass: the heavy victim moves half as far as the attacker.
    CHECK(Close.Right.getController().getKnockback() > 0.0f);
    CHECK(Close.Left.getController().getKnockback() ==
          Approx(-2.0f * Close.Right.getController().getKnockback()).epsilon(0.01));
    Close.run(120);
    CHECK(getPelvisX(Close.Right) - getPelvisX(Close.Left) == Approx(CloseRange).margin(0.03f));

    // Far enough already: nothing.
    CHECK(pushApartOnHit(Close.Left, Close.Right) == 0.0f);
}

TEST_CASE("pushApartOnHit: against the wall the attacker takes all of the push", "[rig]") {
    Duel Corner(2.2f, 2.75f);
    Corner.run(5);
    REQUIRE(Corner.Right.isAgainstWall());
    REQUIRE(pushApartOnHit(Corner.Left, Corner.Right) > 0.0f);
    CHECK(Corner.Right.getController().getKnockback() == 0.0f);
    CHECK(Corner.Left.getController().getKnockback() < 0.0f);
    Corner.run(120);
    CHECK(getPelvisX(Corner.Right) == Approx(2.75f));
    const float CloseRange = Corner.Right.getControl().CloseRange;
    CHECK(getPelvisX(Corner.Right) - getPelvisX(Corner.Left) == Approx(CloseRange).margin(0.03f));
}

TEST_CASE("Rig: a posed leg hits the opponent's posed legs and pelvis", "[rig]") {
    // A low kick: the left fighter swings its front leg into the right
    // one's front shin. Both legs are kinematic; the world reports the hit.
    Duel Low(-0.45f, 0.45f);
    Low.run(10);
    PerBodyPart<float> Kick = loadStance();
    Kick[static_cast<size_t>(BodyPart::ThighL)] = 1.0f;
    Kick[static_cast<size_t>(BodyPart::ShinL)] = 0.0f;
    Low.Left.setTargetAngles(Kick);
    std::vector<physics::HitEvent> Hits;
    for (int Step = 0; Step < 30; ++Step) {
        Low.run(1);
        std::ranges::copy(Low.PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
    const auto IsLegHit = [](const physics::HitEvent& Hit) {
        const bool ByLeftLeg = Hit.Attacker.Fighter == 0 &&
                               (Hit.Attacker.Part == BodyPart::FootL || Hit.Attacker.Part == BodyPart::ShinL);
        const BodyPart Victim = Hit.Victim.Part;
        const bool OnPosed = Victim == BodyPart::Pelvis || Victim == BodyPart::ThighL || Victim == BodyPart::ShinL ||
                             Victim == BodyPart::FootL || Victim == BodyPart::ThighR || Victim == BodyPart::ShinR ||
                             Victim == BodyPart::FootR;
        return ByLeftLeg && OnPosed && Hit.Impulse > 0.0f;
    };
    CHECK(std::ranges::any_of(Hits, IsLegHit));
}

TEST_CASE("Rig: the feet do not slide while walking", "[rig]") {
    const anim::Clip Walk = anim::loadClip(std::filesystem::path(FIGHTER_DATA_DIR) / "poses" / "walk.json");
    Solo Stage(makeSetup(-2.0f, true));
    Rig& Body = Stage.Body;
    Stage.run(10);
    // The cycle at the speed it is made for (combat plays it at the speed
    // actually walked): its feet do not keep pace with the pelvis on the
    // floor, and without planting they slid by 26 cm.
    Body.setMoveVelocity(Body.getWalkSpeed());
    const anim::Clip Stance = anim::loadClip(std::filesystem::path(FIGHTER_DATA_DIR) / "poses" / "stance.json");
    float WalkTime = 0.0f;
    float LongestSlide = 0.0f;
    PerBodyPart<float> LockedAt{};
    PerBodyPart<bool> WasLocked{};
    for (int Step = 0; Step < 120; ++Step) {
        WalkTime = std::fmod(WalkTime + Dt, Walk.DurationSec);
        anim::Pose Pose = anim::sampleClip(Stance, 0.0f);
        anim::layerPose(Pose, anim::sampleClip(Walk, WalkTime));
        Body.setTargetAngles(Pose.Angles);
        Stage.run(1);
        for (const auto Foot : {BodyPart::FootL, BodyPart::FootR}) {
            const auto Index = static_cast<size_t>(Foot);
            // The ankle: the foot rolls about it (humanoid.json, facing right).
            const float X = (Body.getPartPosition(Foot) + rotate({-0.05f, 0.06f}, Body.getPartAngle(Foot))).X;
            const bool Locked = Body.isFootLocked(Foot) && Body.getPartPosition(Foot).Y < 0.06f;
            if (Locked && WasLocked[Index]) LongestSlide = std::max(LongestSlide, std::abs(X - LockedAt[Index]));
            if (Locked && !WasLocked[Index]) LockedAt[Index] = X;
            WasLocked[Index] = Locked;
        }
    }
    CHECK(Body.getPartPosition(BodyPart::Pelvis).X > -0.5f);   // walked
    CHECK(LongestSlide < 0.03f);
}

namespace {

/// The fastest turn of a physical part of \p Body over \p Steps, rad/s:
/// a body at rest that jitters keeps turning back and forth.
float measureJitter(Duel& Stage, const Rig& Body, int Steps) {
    constexpr std::array Physical = {BodyPart::Torso, BodyPart::Head, BodyPart::UpperArmL, BodyPart::ForearmL,
                                     BodyPart::UpperArmR, BodyPart::ForearmR};
    float Fastest = 0.0f;
    for (int Step = 0; Step < Steps; ++Step) {
        PerBodyPart<float> Before{};
        for (const auto Part : Physical) Before[static_cast<size_t>(Part)] = Body.getPartAngle(Part);
        Stage.run(1);
        for (const auto Part : Physical) {
            const float Turn = std::remainder(Body.getPartAngle(Part) - Before[static_cast<size_t>(Part)],
                                              2.0f * std::numbers::pi_v<float>);
            Fastest = std::max(Fastest, std::abs(Turn) / Dt);
        }
    }
    return Fastest;
}

} // namespace

TEST_CASE("Scenario: twice the mass stands and walks and falls and gets up without jitter", "[rig][scenario]") {
    for (const float MassScale : {1.0f, 2.0f}) {
        CAPTURE(MassScale);
        Duel Stage(-1.2f, 1.2f, MassScale);
        Rig& Heavy = Stage.Right;
        const ControlParams& Control = Heavy.getControl();
        Stage.run(60);

        // Stands: upright and still.
        CHECK(measureJitter(Stage, Heavy, 120) < 0.2f);
        CHECK(Heavy.getPartPosition(BodyPart::Head).Y > 1.5f);

        // Walks there and back, upright.
        Heavy.setMoveVelocity(-Heavy.getWalkSpeed());
        Stage.run(60);
        CHECK(Heavy.getPartPosition(BodyPart::Head).Y > 1.5f);
        Heavy.setMoveVelocity(0.0f);
        Stage.run(60);
        CHECK(measureJitter(Stage, Heavy, 60) < 0.2f);

        // Is hit: sways and settles again.
        Heavy.applyHit(30.0f, {1.0f, 0.0f}, Heavy.getPartPosition(BodyPart::Head), false);
        Stage.run(120);
        CHECK(measureJitter(Stage, Heavy, 60) < 0.2f);
        CHECK(Heavy.getPartPosition(BodyPart::Head).Y > 1.5f);

        // Falls, lies still and gets up.
        Heavy.applyHit(60.0f * MassScale, {1.0f, 0.0f}, Heavy.getPartPosition(BodyPart::Head), true);
        Stage.run(static_cast<int>(Control.KnockdownSec / Dt) - 30);
        REQUIRE(Heavy.getPosture() == Posture::KnockedDown);
        CHECK(Heavy.getPartPosition(BodyPart::Head).Y < 0.7f);
        CHECK(measureJitter(Stage, Heavy, 20) < 3.0f);   // limp, no explosion
        // The motors are as strong as for the light body: the heavy one
        // takes a second longer to straighten up, then stands still.
        Stage.run(static_cast<int>(Control.GetUpSec / Dt) + 120);
        CHECK(Heavy.getPosture() == Posture::Standing);
        CHECK(Heavy.getPartPosition(BodyPart::Head).Y > 1.5f);
        CHECK(measureJitter(Stage, Heavy, 60) < 0.2f);
    }
}
