#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <numbers>
#include <optional>
#include <ranges>
#include <vector>

#include "anim/clip.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "rig/contact.hpp"
#include "rig/spacing.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

// The body of task 2.1 without a battle: feet, crouch, turning around,
// knockdown direction, staying down, the weapon, limbs stuck in the
// opponent, walls, the spacing of two fighters (rig/spacing.hpp) and the
// contact stages around the physics step (rig/contact.hpp).

namespace {

constexpr float Dt = 1.0f / 60.0f;
constexpr float ArenaHalfWidth = 3.0f;

RigDef loadHumanoid() { return loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

PerBodyPart<float> loadStance() {
    const anim::Clip Stance = anim::loadClip(std::filesystem::path(FIGHTER_DATA_DIR) / "poses" / "stance.json");
    return anim::sampleClip(Stance, 0.0f).Angles;
}

/// The stance with both legs straight under the hips: the bodies are narrow,
/// so that the spacing of the legs lets the pelvises come as close as the
/// pushbox does (the shipped stance keeps them about 0.75 m apart).
PerBodyPart<float> loadNarrowStance() {
    PerBodyPart<float> Narrow = loadStance();
    for (const auto Part : {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR, BodyPart::ShinR,
                            BodyPart::FootR}) {
        Narrow[static_cast<size_t>(Part)] = 0.0f;
    }
    return Narrow;
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

/// The physics of data/combat.json (physicsSteps, physicsSubSteps,
/// contactHertz, fighterFriction): two fighters press into each other as in
/// a battle.
const physics::World::Config DuelPhysics{
    .StepPasses = 8, .SubSteps = 4, .ContactHertz = 240.0f, .FighterFriction = 0.6f};

/// Two fighters facing each other, kept apart like in a battle.

struct Duel {
    physics::World PhysWorld{DuelPhysics};
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

    /// Both stand in \p Targets from the start.
    void pose(const PerBodyPart<float>& Targets) {
        for (Rig* Body : {&Left, &Right}) {
            Body->setTargetAngles(Targets);
            Body->snapToTargets();
        }
    }

    /// The contact stages of a battle with these spacing parameters.
    ContactResolver makeContacts(float StopDepth = 0.01f) const {
        return ContactResolver({.Spacing = Spacing, .StopDepth = StopDepth});
    }

    /// Steps with the contact stage before the physics step (the spacing),
    /// not the one after it.
    void run(int Steps) {
        ContactResolver Contacts = makeContacts();
        for (int Step = 0; Step < Steps; ++Step) {
            Left.planMotion(Dt);
            Right.planMotion(Dt);
            Contacts.beforeStep(Left, Right, Dt);
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
    Armed.Held = {{.Part = BodyPart::ForearmR, .WeaponReachM = 0.5f}};
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

TEST_CASE("Rig: a limb stuck in the opponent yields and pulls back, still colliding", "[rig]") {
    // The left fighter reaches straight into the right one's chest. The
    // guards start overlapping (the bodies are teleported into the stance):
    // the solver takes them apart first.
    Duel Stuck(-0.35f, 0.35f, 3.0f);
    Stuck.run(30);
    PerBodyPart<float> Reaching = loadStance();
    Reaching[static_cast<size_t>(BodyPart::UpperArmL)] = 1.57f;
    Reaching[static_cast<size_t>(BodyPart::ForearmL)] = 0.0f;
    Stuck.Left.setTargetAngles(Reaching);
    const RigDef Def = loadHumanoid();
    REQUIRE(Def.YieldPosed.test(static_cast<size_t>(BodyPart::ForearmL)));

    // Nothing passes through: the arm is pushed out of the chest (the posed
    // feet are the spacing's, not checked here).
    float Deepest = 0.0f;
    const auto runWatching = [&](int Steps) {
        for (int Step = 0; Step < Steps; ++Step) {
            Stuck.run(1);
            const auto Overlap = Stuck.PhysWorld.findDeepestOverlap();
            if (Overlap && !Def.Kinematic.test(static_cast<size_t>(Overlap->First.Part))) {
                Deepest = std::max(Deepest, Overlap->Depth);
            }
        }
    };
    bool Yielding = false;
    for (int Step = 0; Step < 60 && !Yielding; ++Step) {
        runWatching(1);
        Yielding = Stuck.Left.isYielding(BodyPart::ForearmL);
    }
    REQUIRE(Yielding);
    CHECK(Stuck.Left.isYielding(BodyPart::UpperArmL));   // the whole arm
    CHECK_FALSE(Stuck.Left.isYielding(BodyPart::ForearmR));
    // It pulls back to the yield pose: the elbow bends, from straight (the
    // wish) more than halfway to the yield angle.
    runWatching(10);
    CHECK(Stuck.Left.getJointAngle(BodyPart::ForearmL) > Def.YieldAngles[static_cast<size_t>(BodyPart::ForearmL)] * 0.5f);
    CHECK(Deepest < 0.01f);

    // A new pose that is not a strike (the clip asks for the guard) does not
    // end the yield at once: the limb comes back only when the guard's pose
    // is clear of the opponent (hysteresis, yieldReturnClearance).
    REQUIRE(Stuck.Left.isYielding(BodyPart::ForearmL));
    Stuck.Left.setTargetAngles(loadStance());
    Stuck.run(1);
    CHECK(Stuck.Left.isYielding(BodyPart::ForearmL));
    // A new strike with the limb (its pose away from the one it yielded
    // from) ends the yield at once: it tries again.
    std::bitset<BodyPartCount> Jab;
    Jab.set(static_cast<size_t>(BodyPart::ForearmL));
    Stuck.Left.setStrikingParts(Jab, Jab);
    Stuck.run(1);
    CHECK_FALSE(Stuck.Left.isYielding(BodyPart::ForearmL));
}

TEST_CASE("Rig: a yielding limb stops yielding once it is clear", "[rig]") {
    Duel Stuck(-0.35f, 0.35f, 3.0f);
    PerBodyPart<float> Reaching = loadStance();
    Reaching[static_cast<size_t>(BodyPart::UpperArmL)] = 1.57f;
    Reaching[static_cast<size_t>(BodyPart::ForearmL)] = 0.0f;
    Stuck.Left.setTargetAngles(Reaching);
    for (int Step = 0; Step < 60 && !Stuck.Left.isYielding(BodyPart::ForearmL); ++Step) Stuck.run(1);
    REQUIRE(Stuck.Left.isYielding(BodyPart::ForearmL));
    // Backing off, the arm is clear of the opponent: after yieldSec it
    // drives to the clip again.
    Stuck.Left.setMoveVelocity(-1.0f);
    Stuck.run(60);
    CHECK_FALSE(Stuck.Left.isYielding(BodyPart::ForearmL));
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
    // The right fighter is twice as heavy; both walk into each other. Their
    // legs are narrow: the pushbox keeps them apart, not the legs.
    Duel Clash(-0.5f, 0.5f, 2.0f);
    Clash.pose(loadNarrowStance());
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
    Swapped.pose(loadNarrowStance());
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
    Corner.pose(loadNarrowStance());
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

    // Until it gets up: then the two keep apart as standing fighters.
    Fallen.Left.setMoveVelocity(1.0f);
    int LyingSteps = 0;
    for (int Step = 0; Step < 60; ++Step) {
        Fallen.run(1);
        if (Fallen.Right.getPosture() != Posture::KnockedDown) break;
        ++LyingSteps;
        const float Body = Fallen.Right.getExtentX().Min;
        CHECK(getPelvisX(Fallen.Left) <=
              Approx(Body - Fallen.Spacing.BodyHalfWidth - Fallen.Left.getControl().LyingClearance).margin(0.01f));
    }
    // Long enough for the walk to reach the body.
    CHECK(LyingSteps >= 25);
}

TEST_CASE("pushApartOnHit: a hit at close range pushes the fighters apart", "[rig]") {
    // Narrow legs: the shipped stance keeps the pelvises further apart than
    // the close range.
    Duel Close(-0.26f, 0.26f, 2.0f);
    Close.pose(loadNarrowStance());
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
    Corner.pose(loadNarrowStance());
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
    // A kick at the legs: the left fighter swings its front leg into the right
    // one's front shin. Both legs are kinematic; the world reports the hit.
    Duel Low(-0.45f, 0.45f);
    Low.run(10);
    PerBodyPart<float> Kick = loadStance();
    Kick[static_cast<size_t>(BodyPart::ThighL)] = 1.0f;
    Kick[static_cast<size_t>(BodyPart::ShinL)] = 0.0f;
    Low.Left.setTargetAngles(Kick);
    // A striking leg: the spacing of the bodies does not push the opponent
    // away from it.
    std::bitset<BodyPartCount> Strikers;
    Strikers.set(static_cast<size_t>(BodyPart::ShinL));
    Strikers.set(static_cast<size_t>(BodyPart::FootL));
    Low.Left.setStrikingParts(Strikers, Strikers);
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

TEST_CASE("Rig: a posed kick stops at the opponent's posed legs", "[rig]") {
    // The left fighter swings its front leg straight out at hip height into
    // the right one's legs and pelvis, a little more each step.
    constexpr float Depth = 0.01f;
    std::bitset<BodyPartCount> Strikers;
    Strikers.set(static_cast<size_t>(BodyPart::ShinL));
    Strikers.set(static_cast<size_t>(BodyPart::FootL));
    const auto kick = [&](bool Stop) {
        Duel Kick(-0.5f, 0.5f);
        // The spacing of the bodies leaves the strikers to stopPosedLimbs().
        Kick.Left.setStrikingParts(Strikers, Strikers);
        PerBodyPart<float> Pose = loadStance();
        const float StartThigh = Pose[static_cast<size_t>(BodyPart::ThighL)];
        float Deepest = 0.0f;
        int Stops = 0;
        for (int Step = 1; Step <= 30; ++Step) {
            const float Progress = std::min(1.0f, static_cast<float>(Step) / 15.0f);
            Pose[static_cast<size_t>(BodyPart::ThighL)] = StartThigh + (1.6f - StartThigh) * Progress;
            Pose[static_cast<size_t>(BodyPart::ShinL)] = -0.1f;
            Kick.Left.setTargetAngles(Pose);
            Kick.run(1);
            const bool Stopped = Stop && Kick.Left.stopPosedLimbs(Depth, true).StrikeKept.has_value();
            Stops += Stopped ? 1 : 0;
            CHECK(Kick.Left.isStoppedAtContact() == Stopped);
            for (const auto Part : {BodyPart::ShinL, BodyPart::FootL}) {
                Deepest = std::max(Deepest, Kick.Left.getPosedPenetration(Part));
            }
        }
        return std::pair(Deepest, Stops);
    };
    const auto [Through, NoStops] = kick(false);
    // Nothing else stops a posed leg in time (the spacing only pushes the
    // bodies apart once the leg is in the opponent).
    CHECK(Through > 3.0f * Depth);
    CHECK(NoStops == 0);
    const auto [Held, Stops] = kick(true);
    CHECK(Stops > 0);
    CHECK(Held <= Depth + 1e-3f);
}

TEST_CASE("Rig: pushBody moves the planted feet with the pelvis", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    Rig& Body = Stage.Body;
    Stage.run(10);
    REQUIRE(Body.isFootLocked(BodyPart::FootL));
    REQUIRE(Body.isFootLocked(BodyPart::FootR));
    const float PelvisX = getPelvisX(Body);
    const float FootLX = Body.getPartPosition(BodyPart::FootL).X;
    const float FootRX = Body.getPartPosition(BodyPart::FootR).X;
    // Pushed 1 cm a step for 10 steps: the body slides as a whole.
    for (int Step = 0; Step < 10; ++Step) {
        Body.planMotion(Dt);
        Body.pushBody(0.01f);
        Body.applyControl(Dt);
        Stage.PhysWorld.step(Dt);
    }
    CHECK(getPelvisX(Body) - PelvisX == Approx(0.1f).margin(0.002f));
    CHECK(Body.getPartPosition(BodyPart::FootL).X - FootLX == Approx(0.1f).margin(0.002f));
    CHECK(Body.getPartPosition(BodyPart::FootR).X - FootRX == Approx(0.1f).margin(0.002f));
    CHECK(Body.isFootLocked(BodyPart::FootL));
    CHECK(Body.isFootLocked(BodyPart::FootR));

    // The prediction of the spacing does the same for a pelvis away from
    // the plan: every part it lists moves along.
    Body.planMotion(Dt);
    const float Planned = Body.getController().getPlannedX();
    const std::vector<PartPlacement> Here = Body.predictBody(Planned, Dt);
    const std::vector<PartPlacement> There = Body.predictBody(Planned + 0.05f, Dt);
    REQUIRE(Here.size() == There.size());
    for (auto&& [Near, Far] : std::views::zip(Here, There)) {
        CHECK(Far.Position.X - Near.Position.X == Approx(0.05f).margin(1e-3f));
    }
}

TEST_CASE("Rig: measureLegs tells where the feet of a pose stand", "[rig]") {
    for (const bool FacingRight : {true, false}) {
        INFO("facing right " << FacingRight);
        Solo Stage(makeSetup(0.3f, FacingRight));
        const Rig& Body = Stage.Body;
        Stage.run(5);
        // The pose measured and the body standing in it agree.
        const LegStance Posed = Body.measureLegs(loadStance());
        const LegStance Now = Body.measureLegsNow();
        CHECK(Posed.PelvisHeight == Approx(Now.PelvisHeight).margin(1e-3f));
        for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
            INFO(getBodyPartName(Foot));
            CHECK(Posed.getFoot(Foot).Ankle.X == Approx(Now.getFoot(Foot).Ankle.X).margin(1e-3f));
            CHECK(Posed.getFoot(Foot).Ankle.Y == Approx(Now.getFoot(Foot).Ankle.Y).margin(1e-3f));
            CHECK(Posed.getFoot(Foot).Angle == Approx(Now.getFoot(Foot).Angle).margin(1e-3f));
            CHECK(Now.getFoot(Foot).SoleHeight == Approx(Posed.getFoot(Foot).SoleHeight).margin(0.006f));
        }
        // The stance: the left foot in front, both on the floor.
        CHECK(Posed.getFrontFoot() == BodyPart::FootL);
        CHECK(Posed.getSpread() > 0.1f);
        CHECK(std::min(Posed.Left.SoleHeight, Posed.Right.SoleHeight) == Approx(0.0f).margin(1e-4f));
    }
}

TEST_CASE("Rig: kept feet hold their place, the knee and the pelvis give only so much", "[rig]") {
    // The pelvis travels with the legs in the stance. A kept foot (combat's
    // keepFeetPlanted) holds its place while the leg reaches it within the
    // caps: the knee no more than kneeExtraBend deeper than the clip, the
    // pelvis no more than maxPelvisDrop down; beyond that it is dragged
    // (combat steps it again).
    struct Result {
        float FootMove = 0.0f;
        float PelvisDrop = 0.0f;
        float DeepestExtraBend = 0.0f;
    };
    const auto travel = [](bool Keep, int Ticks) {
        Solo Stage(makeSetup(0.0f, true));
        Rig& Body = Stage.Body;
        Stage.run(10);
        Result Out;
        // The ankle in the world (the foot may turn about it).
        const auto getAnkleX = [&] {
            return Body.getPartPosition(BodyPart::Pelvis).X + Body.measureLegsNow().Right.Ankle.X;
        };
        const float AnkleX = getAnkleX();
        const float PelvisY = Body.getPartPosition(BodyPart::Pelvis).Y;
        const PerBodyPart<float> Stance = loadStance();
        for (int Step = 0; Step < Ticks + 16; ++Step) {
            if (Keep) Body.keepFeetPlanted();
            Body.setMoveVelocity(Step < Ticks ? 1.0f : 0.0f);
            Body.planMotion(Dt);
            Body.applyControl(Dt);
            Stage.PhysWorld.step(Dt);
            for (const BodyPart Shin : {BodyPart::ShinL, BodyPart::ShinR}) {
                const float Extra = Stance[static_cast<size_t>(Shin)] - Body.getJointAngle(Shin);
                Out.DeepestExtraBend = std::max(Out.DeepestExtraBend, Extra);
            }
            Out.PelvisDrop = std::max(Out.PelvisDrop, PelvisY - Body.getPartPosition(BodyPart::Pelvis).Y);
        }
        Out.FootMove = std::abs(getAnkleX() - AnkleX);
        return Out;
    };
    const ControlParams& Control = loadHumanoid().Control;
    // A short travel: the kept foot stays, a plain one is held as well
    // (within footLockSlip).
    const Result Short = travel(true, 6);
    CHECK(Short.FootMove < 1e-3f);
    // A long one: plain feet are dragged after footLockSlip; kept feet hold
    // as long as the caps allow and are then dragged too, the knee and the
    // pelvis never beyond the caps.
    const Result Dragged = travel(false, 14);
    const Result Kept = travel(true, 14);
    CHECK(Dragged.FootMove > 0.03f);
    CHECK(Kept.PelvisDrop <= Control.MaxPelvisDrop + 1e-3f);
    CHECK(Kept.DeepestExtraBend <= Control.KneeExtraBend + 0.005f);
    CHECK(Dragged.DeepestExtraBend <= Control.KneeExtraBend + 0.005f);
}

TEST_CASE("Rig: setPelvisDropLimit lets the pelvis go down deeper for a kept foot", "[rig][pelvis]") {
    // A lunge: the pelvis travels away from a kept rear foot; with a deeper
    // limit the pelvis goes down further (never beyond it) and the foot
    // holds its place longer.
    struct Result {
        float FootMove = 0.0f;
        float PelvisDrop = 0.0f;
    };
    const auto travel = [](std::optional<float> Limit) {
        Solo Stage(makeSetup(0.0f, true));
        Rig& Body = Stage.Body;
        Stage.run(10);
        const auto getAnkleX = [&] {
            return Body.getPartPosition(BodyPart::Pelvis).X + Body.measureLegsNow().Right.Ankle.X;
        };
        const float AnkleX = getAnkleX();
        const float PelvisY = Body.getPartPosition(BodyPart::Pelvis).Y;
        Result Out;
        for (int Step = 0; Step < 14; ++Step) {
            Body.keepFeetPlanted();
            Body.setPelvisDropLimit(Limit);
            Body.getController().setClipTravel(0.015f);
            Body.planMotion(Dt);
            Body.applyControl(Dt);
            Stage.PhysWorld.step(Dt);
            Out.PelvisDrop = std::max(Out.PelvisDrop, PelvisY - Body.getPartPosition(BodyPart::Pelvis).Y);
        }
        Out.FootMove = std::abs(getAnkleX() - AnkleX);
        return Out;
    };
    const ControlParams& Control = loadHumanoid().Control;
    const Result Default = travel(std::nullopt);
    const Result Deep = travel(0.1f);
    INFO("default: drop " << Default.PelvisDrop << " m, foot " << Default.FootMove << " m; deep: drop "
                          << Deep.PelvisDrop << " m, foot " << Deep.FootMove << " m");
    CHECK(Default.PelvisDrop <= Control.MaxPelvisDrop + 1e-3f);
    CHECK(Deep.PelvisDrop > Control.MaxPelvisDrop + 0.01f);
    CHECK(Deep.PelvisDrop <= 0.1f + 1e-3f);
    CHECK(Deep.FootMove < Default.FootMove);
}

TEST_CASE("Rig: dropLiftedFootOffsets puts a lifted foot where the pose has it", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    Rig& Body = Stage.Body;
    Stage.run(10);
    // A push leaves the planted feet behind the pelvis; a lifted foot then
    // returns to the pose from where it stood, over time.
    Body.addPush(0.1f);
    Stage.run(15);
    PerBodyPart<float> Lifted = loadStance();
    Lifted[static_cast<size_t>(BodyPart::ThighL)] = 1.2f;
    Lifted[static_cast<size_t>(BodyPart::ShinL)] = -1.6f;
    const auto lift = [&](bool Drop) {
        Body.setTargetAngles(Lifted);
        Body.planMotion(Dt);
        if (Drop) Body.dropLiftedFootOffsets();
        Body.applyControl(Dt);
        Stage.PhysWorld.step(Dt);
    };
    lift(false);   // lifted: unlocked, with its offset
    REQUIRE_FALSE(Body.isFootLocked(BodyPart::FootL));
    const float Posed = Body.measureLegs(Lifted).Left.Ankle.X;
    lift(false);
    const float Kept = Body.measureLegsNow().Left.Ankle.X;
    lift(true);
    const float Dropped = Body.measureLegsNow().Left.Ankle.X;
    CHECK(std::abs(Dropped - Posed) < 0.005f);
    CHECK(std::abs(Kept - Posed) > std::abs(Dropped - Posed));
}

TEST_CASE("Rig: reachFoot bends a leg to put its ankle at a point", "[rig]") {
    for (const bool FacingRight : {true, false}) {
        INFO("facing right " << FacingRight);
        Solo Stage(makeSetup(0.0f, FacingRight));
        const Rig& Body = Stage.Body;
        const PerBodyPart<float> Stance = loadStance();
        const LegStance Before = Body.measureLegs(Stance);
        // The left ankle 10 cm further forward and 6 cm up, the foot level.
        PerBodyPart<float> Angles = Stance;
        const Vec2 Target = Before.Left.Ankle + Vec2{0.1f, 0.06f};
        Body.reachFoot(Angles, BodyPart::FootL, Before.PelvisHeight, Target, 0.0f);
        const LegStance After = Body.measureLegs(Angles);
        // The right leg did not change and still carries the body.
        CHECK(After.PelvisHeight == Approx(Before.PelvisHeight).margin(1e-4f));
        CHECK(After.Right.Ankle.X == Approx(Before.Right.Ankle.X).margin(1e-4f));
        CHECK(After.Left.Ankle.X == Approx(Target.X).margin(1e-3f));
        CHECK(After.Left.Ankle.Y == Approx(Target.Y).margin(1e-3f));
        CHECK(After.Left.Angle == Approx(0.0f).margin(1e-3f));
        CHECK(After.Left.SoleHeight > 0.03f);
        // The knee bends the way a knee bends (negative facing right).
        CHECK(Angles[static_cast<size_t>(BodyPart::ShinL)] < 0.0f);
        // Out of reach: the leg stretches towards the point.
        PerBodyPart<float> Far = Stance;
        Body.reachFoot(Far, BodyPart::FootR, Before.PelvisHeight, {-1.5f, 0.0f}, 0.0f);
        CHECK(Body.measureLegs(Far).Right.Ankle.X < Before.Right.Ankle.X - 0.2f);
    }
}

TEST_CASE("Rig: stopPosedLimbs holds back a leg swung into the opponent", "[rig]") {
    // The left fighter swings its front leg into the right one's legs in
    // big steps; the spacing is too slow to keep them apart. The leg is no
    // striker: stopPosedLimbs holds it at the opponent.
    constexpr float Depth = 0.01f;
    const auto swing = [&](bool Hold) {
        Duel Swing(-0.5f, 0.5f);
        Swing.Spacing.PosedSeparationSpeed = 1e-3f;
        Swing.Spacing.SeparationSpeed = 1e-3f;
        Swing.run(5);
        PerBodyPart<float> Pose = loadStance();
        Pose[static_cast<size_t>(BodyPart::ThighL)] = 1.6f;
        Pose[static_cast<size_t>(BodyPart::ShinL)] = -0.1f;
        Swing.Left.setTargetAngles(Pose);
        float Deepest = 0.0f;
        for (int Step = 0; Step < 20; ++Step) {
            Swing.run(1);
            if (Hold) Swing.Left.stopPosedLimbs(Depth, false);
            for (const auto Part : {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL}) {
                Deepest = std::max(Deepest, Swing.Left.getPosedPenetration(Part));
            }
        }
        return Deepest;
    };
    CHECK(swing(false) > 3.0f * Depth);
    CHECK(swing(true) <= Depth + 1e-3f);
}

TEST_CASE("Rig: the spacing sees a striker where it is and where the clip takes it", "[rig]") {
    // The left fighter is about to swing its front leg straight out into the
    // right one: the clip's pose of the leg overlaps the opponent, where it
    // is now does not.
    Duel Kick(-0.5f, 0.5f);
    Kick.run(5);
    PerBodyPart<float> Pose = loadStance();
    Pose[static_cast<size_t>(BodyPart::ThighL)] = 1.6f;
    Pose[static_cast<size_t>(BodyPart::ShinL)] = -0.1f;
    Kick.Left.setTargetAngles(Pose);
    Kick.Left.planMotion(Dt);
    Kick.Right.planMotion(Dt);
    const auto getGap = [&] {
        return Kick.Left.measureGap(Kick.Left.predictBody(Kick.Left.getController().getPlannedX(), Dt),
                                    Kick.Right.predictBody(Kick.Right.getController().getPlannedX(), Dt));
    };
    const size_t Parts = Kick.Left.predictBody(Kick.Left.getController().getPlannedX(), Dt).size();
    // A plain leg overlaps where it goes: the spacing would push the
    // opponent away before the kick lands.
    CHECK(getGap() < 0.0f);
    // A striker does not overlap: it overlaps only if it does in both places.
    std::bitset<BodyPartCount> Strikers;
    Strikers.set(static_cast<size_t>(BodyPart::ShinL));
    Strikers.set(static_cast<size_t>(BodyPart::FootL));
    Kick.Left.setStrikingParts(Strikers, Strikers);
    CHECK(getGap() >= 0.0f);
    // The lifted kicking foot keeps no floor below it clear (one shadow
    // placement fewer).
    CHECK(Kick.Left.predictBody(Kick.Left.getController().getPlannedX(), Dt).size() == Parts - 1);
}

TEST_CASE("Rig: a push that only takes back the planned travel leaves the feet planted", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    Rig& Body = Stage.Body;
    Stage.run(10);
    REQUIRE(Body.isFootLocked(BodyPart::FootL));
    const float FootLX = Body.getPartPosition(BodyPart::FootL).X;
    // The pelvis plans to walk 1 cm a step, the spacing takes all of it back:
    // the fighter just stands, its feet where they were.
    Body.setMoveVelocity(0.6f);
    for (int Step = 0; Step < 10; ++Step) {
        Body.planMotion(Dt);
        Body.pushBody(-Body.getController().getPlannedTravel());
        Body.applyControl(Dt);
        Stage.PhysWorld.step(Dt);
    }
    CHECK(Body.getPartPosition(BodyPart::FootL).X == Approx(FootLX).margin(1e-3f));
    // Pushed beyond the start of the step, the planted feet go along with
    // the part beyond it.
    Body.planMotion(Dt);
    const float Planned = Body.getController().getPlannedX();
    const float Start = Body.getController().getPositionX();
    const std::vector<PartPlacement> AtStart = Body.predictBody(Start, Dt);
    const std::vector<PartPlacement> Behind = Body.predictBody(Start - 0.05f, Dt);
    REQUIRE(Planned > Start);
    REQUIRE(AtStart.size() == Behind.size());
    for (auto&& [Near, Far] : std::views::zip(AtStart, Behind)) {
        CHECK(Near.Position.X - Far.Position.X == Approx(0.05f).margin(1e-3f));
    }
}

TEST_CASE("Rig: the posed joints follow the share of the travel made", "[rig]") {
    // The front leg is lifted (a step), so no planted foot holds it. The pose
    // of the step assumes the planned travel; without it the thigh is 0.2 rad
    // further back. The spacing lets the pelvis make a share of the travel:
    // the thigh is posed that share of the way.
    PerBodyPart<float> Moving = loadStance();
    Moving[static_cast<size_t>(BodyPart::ThighL)] = 0.8f;
    Moving[static_cast<size_t>(BodyPart::ShinL)] = -1.0f;
    PerBodyPart<float> Still = Moving;
    Still[static_cast<size_t>(BodyPart::ThighL)] -= 0.2f;
    const auto getThigh = [&](float Share) {
        Solo Stage(makeSetup(0.0f, true), Moving);
        Rig& Body = Stage.Body;
        Stage.run(10);
        REQUIRE_FALSE(Body.isFootLocked(BodyPart::FootL));
        Body.setMoveVelocity(1.2f);
        Body.planMotion(Dt);
        const float Travel = Body.getController().getPlannedTravel();
        Body.setTargetAngles(Moving);
        Body.setTravelPose(Still, Travel);
        CHECK(Body.getTravelShare() == 1.0f);
        Body.pushBody(-Travel * (1.0f - Share));
        CHECK(Body.getTravelShare() == Approx(Share).margin(1e-4f));
        Body.applyControl(Dt);
        Stage.PhysWorld.step(Dt);
        return Body.getPartAngle(BodyPart::ThighL);
    };
    const float None = getThigh(0.0f);
    const float Half = getThigh(0.5f);
    const float All = getThigh(1.0f);
    REQUIRE(std::abs(All - None) > 0.1f);
    CHECK(Half == Approx((None + All) * 0.5f).margin(0.02f));
}

TEST_CASE("Rig: isStriking tells posed strikers", "[rig]") {
    Solo Stage(makeSetup(0.0f, true));
    CHECK_FALSE(Stage.Body.isStriking());
    std::bitset<BodyPartCount> Strikers;
    Strikers.set(static_cast<size_t>(BodyPart::FootL));
    Stage.Body.setStrikingParts(Strikers, Strikers);
    CHECK(Stage.Body.isStriking());
}

TEST_CASE("keepApart: a push apart grows by pushAcceleration and stops at the contact", "[rig]") {
    // Two standing fighters overlap by 10 cm (the pushboxes): the push
    // apart eases in, not faster than PushMaxSpeed, and stops where they
    // touch.
    Duel Close(-0.2f, 0.2f);
    Close.pose(loadNarrowStance());
    Close.Spacing.PushAcceleration = 20.0f;
    Close.Spacing.PushMaxSpeed = 1.5f;
    const float MinGap = 2.0f * Close.Spacing.BodyHalfWidth;
    float LastSpeed = 0.0f;
    float Fastest = 0.0f;
    for (int Step = 0; Step < 60; ++Step) {
        const float Before = getPelvisX(Close.Right) - getPelvisX(Close.Left);
        Close.run(1);
        const float Speed = (getPelvisX(Close.Right) - getPelvisX(Close.Left) - Before) / Dt;
        CHECK(Speed <= LastSpeed + Close.Spacing.PushAcceleration * Dt + 1e-3f);
        Fastest = std::max(Fastest, Speed);
        LastSpeed = Speed;
    }
    CHECK(Fastest <= Close.Spacing.PushMaxSpeed + 1e-3f);
    CHECK(getPelvisX(Close.Right) - getPelvisX(Close.Left) == Approx(MinGap).margin(1e-3f));
    CHECK(Close.Right.getController().getSpacingMotion().Pushed == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("Rig: a weapon in the left hand extends the left forearm", "[rig][hands]") {
    // The left arm straight forward.
    PerBodyPart<float> Reaching = loadStance();
    Reaching[static_cast<size_t>(BodyPart::UpperArmL)] = 1.57f;
    Reaching[static_cast<size_t>(BodyPart::ForearmL)] = 0.0f;
    RigSetup Armed = makeSetup(0.0f, true);
    Armed.Held = {{.Part = BodyPart::ForearmL, .WeaponReachM = 0.5f, .WeaponWidthM = 0.08f}};
    Solo Bare(makeSetup(0.0f, true), Reaching);
    Solo Sword(Armed, Reaching);
    Bare.run(60);
    Sword.run(60);
    CHECK(Sword.Body.getWeaponReach() == 0.5f);
    CHECK(Sword.Body.getWeaponReach(BodyPart::ForearmL) == 0.5f);
    CHECK(Sword.Body.getWeaponReach(BodyPart::ForearmR) == 0.0f);
    CHECK(Sword.Body.getExtentX().Max == Approx(Bare.Body.getExtentX().Max + 0.5f).margin(0.03f));

    // Turned 90 degrees to the forearm, the blade points down: no reach forward.
    RigSetup Down = makeSetup(0.0f, true);
    Down.Held = {{.Part = BodyPart::ForearmL, .WeaponReachM = 0.5f, .WeaponAngleDeg = -90.0f}};
    Solo Turned(Down, Reaching);
    Turned.run(60);
    CHECK(Turned.Body.getExtentX().Max < Bare.Body.getExtentX().Max + 0.1f);
}

TEST_CASE("Rig: a shield is a plate on the forearm that holds it", "[rig][hands]") {
    RigSetup Setup = makeSetup(0.0f, true);
    Setup.Held = {{.Part = BodyPart::ForearmL, .ShieldLengthM = 0.5f, .ShieldWidthM = 0.4f, .ShieldGuards = true}};
    Solo Bare(makeSetup(0.0f, true));
    Solo Shielded(Setup);
    Bare.run(30);
    Shielded.run(30);
    CHECK(Shielded.Body.hasShield(BodyPart::ForearmL));
    CHECK_FALSE(Shielded.Body.hasShield(BodyPart::ForearmR));
    CHECK_FALSE(Bare.Body.isOnShield(Bare.Body.getPartPosition(BodyPart::ForearmL)));
    CHECK(Shielded.Body.getWeaponReach() == 0.0f);
    // The plate covers the forearm and reaches about half its width past it.
    const Vec2 Forearm = Shielded.Body.getPartPosition(BodyPart::ForearmL);
    CHECK(Shielded.Body.isOnShield(Forearm));
    CHECK_FALSE(Shielded.Body.isOnShield(Forearm + Vec2{0.5f, 0.0f}));
    CHECK(Shielded.Body.isOnShield(Forearm + Vec2{0.5f, 0.0f}, 0.5f));
    const float Wider = Shielded.Body.getExtentX().Max - Bare.Body.getExtentX().Max;
    CHECK(Wider > 0.05f);
    CHECK(Shielded.Body.getTotalMass() == Approx(Bare.Body.getTotalMass()));

    // Turned around, the plate is still on the forearm.
    Shielded.Body.setFacing(false);
    Shielded.run(30);
    REQUIRE_FALSE(Shielded.Body.isFacingRight());
    CHECK(Shielded.Body.isOnShield(Shielded.Body.getPartPosition(BodyPart::ForearmL)));
}

TEST_CASE("Rig: a shield that does not guard is only a part of the forearm", "[rig][hands]") {
    // A shield in the main hand (decision 2026-10-08): the same plate, but a
    // hit on it is not "on the shield".
    RigSetup Setup = makeSetup(0.0f, true);
    Setup.Held = {{.Part = BodyPart::ForearmR, .ShieldLengthM = 0.5f, .ShieldWidthM = 0.4f}};
    Solo Bare(makeSetup(0.0f, true));
    Solo Shielded(Setup);
    Bare.run(30);
    Shielded.run(30);
    CHECK(Shielded.Body.hasShield(BodyPart::ForearmR));
    CHECK_FALSE(Shielded.Body.isOnShield(Shielded.Body.getPartPosition(BodyPart::ForearmR)));
    CHECK(Shielded.Body.getExtentX().Max - Bare.Body.getExtentX().Max > 0.05f);
}

TEST_CASE("Rig: the other hand grips a two-handed weapon", "[rig][hands]") {
    RigSetup Setup = makeSetup(0.0f, true);
    Setup.Held = {{.Part = BodyPart::ForearmR, .WeaponReachM = 0.9f}};
    RigSetup Gripping = Setup;
    Gripping.GripPart = BodyPart::ForearmL;
    Solo Loose(Setup);
    Solo Held(Gripping);
    const ControlParams& Control = Held.Body.getControl();
    CHECK_FALSE(Loose.Body.getGripPart().has_value());
    CHECK(Loose.Body.getGripGap() == 0.0f);
    REQUIRE(Held.Body.getGripPart() == BodyPart::ForearmL);
    Loose.run(60);
    Held.run(60);
    // The spring keeps the left fist on the handle, within its stretch.
    CHECK(Held.Body.getGripGap() <= Control.GripMaxStretch + 0.01f);
    // ...also after turning around.
    Held.Body.setFacing(false);
    Held.run(60);
    REQUIRE_FALSE(Held.Body.isFacingRight());
    CHECK(Held.Body.getGripGap() <= Control.GripMaxStretch + 0.01f);
}

TEST_CASE("ContactResolver: both fighters' legs swung into each other stop at the contact", "[rig][contact]") {
    // Both swing their front legs into each other in big steps; the spacing
    // is too slow to keep them apart. Without the stage after the step the
    // legs go through each other; with it neither goes deeper than the stop
    // depth, though each one's stop moves its leg back from where the other
    // one's stop saw it.
    constexpr float Depth = 0.01f;
    const auto swing = [&](bool Stop) {
        Duel Swing(-0.45f, 0.45f);
        Swing.Spacing.PosedSeparationSpeed = 1e-3f;
        Swing.Spacing.SeparationSpeed = 1e-3f;
        Swing.run(5);
        ContactResolver Contacts = Swing.makeContacts(Depth);
        PerBodyPart<float> Pose = loadStance();
        Pose[static_cast<size_t>(BodyPart::ThighL)] = 1.6f;
        Pose[static_cast<size_t>(BodyPart::ShinL)] = -0.1f;
        Swing.Left.setTargetAngles(Pose);
        Swing.Right.setTargetAngles(Pose);
        float Deepest = 0.0f;
        std::bitset<BodyPartCount> Held;
        bool Again = false;
        for (int Step = 0; Step < 20; ++Step) {
            Swing.Left.planMotion(Dt);
            Swing.Right.planMotion(Dt);
            Contacts.beforeStep(Swing.Left, Swing.Right, Dt);
            Swing.Left.applyControl(Dt);
            Swing.Right.applyControl(Dt);
            Swing.PhysWorld.step(Dt);
            if (Stop) {
                const std::array<PosedStop, 2> Stops = Contacts.afterStep(Swing.Left, Swing.Right);
                // No attack: no strikers to report.
                CHECK_FALSE(Stops[0].StrikeKept.has_value());
                CHECK_FALSE(Stops[1].StrikeKept.has_value());
                Held |= Stops[0].HeldLimbs | Stops[1].HeldLimbs;
                CHECK(Contacts.getPosedPasses() >= 2);
                Again = Again || Contacts.getPosedPasses() > 2;
                CHECK(Contacts.getPosedPasses() <= ContactResolver::MaxPosedPasses);
            }
            for (const Rig* Body : {&Swing.Left, &Swing.Right}) {
                for (const auto Part : {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL}) {
                    Deepest = std::max(Deepest, Body->getPosedPenetration(Part));
                }
            }
        }
        if (Stop) {
            CHECK(Held.test(static_cast<size_t>(BodyPart::ThighL)));
            // The second one held a leg back after the first one stopped: the
            // first one went again.
            CHECK(Again);
        }
        return Deepest;
    };
    CHECK(swing(false) > 3.0f * Depth);
    CHECK(swing(true) <= Depth + 1e-3f);
}

TEST_CASE("ContactResolver: the strikers of an attack stop at a touch and tell the share kept", "[rig][contact]") {
    constexpr float Depth = 0.01f;
    std::bitset<BodyPartCount> Strikers;
    Strikers.set(static_cast<size_t>(BodyPart::ShinL));
    Strikers.set(static_cast<size_t>(BodyPart::FootL));
    Duel Kick(-0.5f, 0.5f);
    Kick.Left.setStrikingParts(Strikers, Strikers);
    ContactResolver Contacts = Kick.makeContacts(Depth);
    PerBodyPart<float> Pose = loadStance();
    const float StartThigh = Pose[static_cast<size_t>(BodyPart::ThighL)];
    std::optional<float> FirstKept;
    for (int Step = 1; Step <= 30 && !FirstKept; ++Step) {
        const float Progress = std::min(1.0f, static_cast<float>(Step) / 15.0f);
        Pose[static_cast<size_t>(BodyPart::ThighL)] = StartThigh + (1.6f - StartThigh) * Progress;
        Pose[static_cast<size_t>(BodyPart::ShinL)] = -0.1f;
        Kick.Left.setTargetAngles(Pose);
        Kick.Left.planMotion(Dt);
        Kick.Right.planMotion(Dt);
        Contacts.beforeStep(Kick.Left, Kick.Right, Dt);
        Kick.Left.applyControl(Dt);
        Kick.Right.applyControl(Dt);
        Kick.PhysWorld.step(Dt);
        const std::array<PosedStop, 2> Stops = Contacts.afterStep(Kick.Left, Kick.Right);
        CHECK_FALSE(Stops[1].StrikeKept.has_value());
        FirstKept = Stops[0].StrikeKept;
        CHECK(Kick.Left.isStoppedAtContact() == FirstKept.has_value());
    }
    REQUIRE(FirstKept.has_value());
    CHECK(*FirstKept >= 0.0f);
    CHECK(*FirstKept <= 1.0f);
    for (const auto Part : {BodyPart::ShinL, BodyPart::FootL}) {
        CHECK(Kick.Left.getPosedPenetration(Part) <= Depth + 1e-3f);
    }
}

TEST_CASE("ContactResolver: posed limbs only touch a fighter lying on the floor", "[rig][contact]") {
    Duel Fallen(-1.0f, 0.2f);
    ContactResolver Contacts = Fallen.makeContacts(0.01f);
    Fallen.Left.planMotion(Dt);
    Fallen.Right.planMotion(Dt);
    Contacts.beforeStep(Fallen.Left, Fallen.Right, Dt);
    CHECK(Contacts.getStopDepth(0) == 0.01f);
    CHECK(Contacts.getStopDepth(1) == 0.01f);
    Fallen.Right.applyHit(100.0f, {1.0f, 0.0f}, Fallen.Right.getPartPosition(BodyPart::Head), true);
    REQUIRE(Fallen.Right.getPosture() == Posture::KnockedDown);
    Fallen.Left.planMotion(Dt);
    Fallen.Right.planMotion(Dt);
    Contacts.beforeStep(Fallen.Left, Fallen.Right, Dt);
    CHECK(Contacts.getStopDepth(0) == 0.0f);
    CHECK(Contacts.getStopDepth(1) == 0.01f);
}

TEST_CASE("ContactResolver: a hit at close range pushes apart, an overlap is judged by its pair", "[rig][contact]") {
    Duel Close(-0.26f, 0.26f, 2.0f);
    Close.pose(loadNarrowStance());
    Close.run(5);
    ContactResolver Contacts({.Spacing = Close.Spacing, .ArmOverlapTolerance = 0.03f, .OverlapTolerance = 0.01f});
    const float Deficit = Close.Right.getControl().CloseRange - (getPelvisX(Close.Right) - getPelvisX(Close.Left));
    REQUIRE(Deficit > 0.1f);
    CHECK(Contacts.onStrikeLanded(Close.Left, Close.Right) == Approx(Deficit));
    CHECK(Close.Right.getController().getPushOut() > 0.0f);

    const physics::PartOverlap Arms{.First = {0, BodyPart::ForearmL}, .Second = {1, BodyPart::UpperArmR}};
    const physics::PartOverlap ArmOnLeg{.First = {0, BodyPart::ForearmL}, .Second = {1, BodyPart::ShinR}};
    CHECK(Contacts.getOverlapTolerance(Arms) == 0.03f);
    CHECK(Contacts.getOverlapTolerance(ArmOnLeg) == 0.01f);
    // Spawned inside each other: the worst overlap is the one furthest
    // beyond its tolerance.
    Duel Inside(-0.1f, 0.1f);
    const std::optional<physics::PartOverlap> Worst = Contacts.findWorstOverlap(Inside.PhysWorld);
    REQUIRE(Worst.has_value());
    for (const auto& Overlap : Inside.PhysWorld.findOverlaps()) {
        CHECK(Overlap.Depth - Contacts.getOverlapTolerance(Overlap) <=
              Worst->Depth - Contacts.getOverlapTolerance(*Worst));
    }
    // Far apart nothing overlaps.
    Duel Apart(-1.5f, 1.5f);
    CHECK_FALSE(Contacts.findWorstOverlap(Apart.PhysWorld).has_value());
}
