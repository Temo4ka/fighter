#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/equipment.hpp"
#include "stats/loading.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::rig;

// Scenario tests of the smooth body: the physical upper body follows the
// clip while the fighter walks, stops and turns, whatever its stats and
// armour, and physics still shows on hits and knockdowns. The rig is driven
// directly with the stance and walk clips, without combat.

namespace {

constexpr float Dt = 1.0f / 60.0f;
constexpr int TicksPerSecond = 60;
/// The largest angle (rad) a physical part may be off the clip's pose while
/// the fighter walks, stops and turns: about 3 degrees. Without the smooth
/// body a heavy and weak fighter's arms flailed by more than 2 rad.
constexpr float MaxWalkPoseError = 0.05f;

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

/// A fighter sheet for the tests: stats and item ids.
struct Sheet {
    std::string Name;
    stats::Stats BaseStats;
    std::vector<std::string> Items;
};

const std::vector<std::string> KnightItems = {"iron_helmet",   "chainmail", "iron_gauntlets",
                                              "plate_greaves", "iron_boots", "war_hammer"};
const std::vector<std::string> RogueItems = {"leather_cap",    "leather_vest", "leather_gloves",
                                             "cloth_trousers", "soft_boots",   "short_sword"};

std::vector<Sheet> getSheets() {
    const stats::FighterSheet Knight = stats::loadFighterSheet(DataDir / "fighters" / "knight.json");
    const stats::FighterSheet Rogue = stats::loadFighterSheet(DataDir / "fighters" / "rogue.json");
    return {
        {.Name = "knight", .BaseStats = Knight.BaseStats, .Items = Knight.ItemIds},
        {.Name = "rogue", .BaseStats = Rogue.BaseStats, .Items = Rogue.ItemIds},
        // The extremes of the legal stats: heavy and weak in full iron, and
        // light and strong in cloth.
        {.Name = "heavy weak", .BaseStats = {.Strength = 1, .Dexterity = 10, .Constitution = 30}, .Items = KnightItems},
        {.Name = "light strong", .BaseStats = {.Strength = 30, .Dexterity = 10, .Constitution = 1}, .Items = RogueItems},
    };
}

RigSetup makeSetup(const Sheet& Fighter, float OriginX, bool FacingRight, uint8_t Index) {
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const stats::Loadout Gear = stats::buildLoadout(Fighter.Items, Catalog);
    const stats::PhysicalProfile Profile =
        stats::computeProfile(Fighter.BaseStats, Gear, stats::loadBalanceTable(DataDir / "balance.json"));
    RigSetup Setup;
    Setup.Origin = {OriginX, 0.005f};
    Setup.FacingRight = FacingRight;
    Setup.FighterIndex = Index;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    Setup.MoveSpeedScale = Profile.MoveSpeedScale;
    if (const stats::WeaponProps* Weapon = Gear.findWeapon()) Setup.WeaponReachM = Weapon->ReachM;
    return Setup;
}

anim::Pose getStance() { return anim::sampleClip(anim::loadClip(DataDir / "poses" / "stance.json"), 0.0f); }

/// One or two rigs standing on a floor in the stance.
struct Stage {
    physics::World PhysWorld;
    RigDef Def;
    std::vector<std::unique_ptr<Rig>> Bodies;
    anim::Clip Walk = anim::loadClip(DataDir / "poses" / "walk.json");
    anim::Pose Stance = getStance();
    float WalkTime = 0.0f;

    explicit Stage(RigDef Description) : Def(std::move(Description)) {
        const physics::Body Ground = PhysWorld.createBody({.Type = physics::BodyType::Static});
        PhysWorld.addShape(Ground, {.Kind = physics::ShapeKind::Box, .Center = {0.0f, -0.5f},
                                    .HalfExtents = {20.0f, 0.5f}});
    }

    Rig& add(const RigSetup& Setup) {
        Rig& Body = *Bodies.emplace_back(std::make_unique<Rig>(PhysWorld, Def, Setup));
        Body.setTargetAngles(Stance.Angles);
        Body.snapToTargets();
        return Body;
    }

    /// One step: \p Body walks at \p Speed times its walking speed (the
    /// walk clip over the stance) or stands; the others stand.
    void step(float Speed = 0.0f) {
        anim::Pose Shown = Stance;
        if (Speed != 0.0f) {
            WalkTime += Dt;
            anim::layerPose(Shown, anim::sampleClip(Walk, WalkTime));
        }
        for (auto&& [Index, Body] : std::views::zip(std::views::iota(size_t{0}), Bodies)) {
            Body->setTargetAngles(Index == 0 ? Shown.Angles : Stance.Angles);
            Body->setMoveVelocity(Index == 0 ? Speed * Body->getWalkSpeed() : 0.0f);
            Body->planMotion(Dt);
        }
        for (auto& Body : Bodies) Body->applyControl(Dt);
        PhysWorld.step(Dt);
    }
};

/// Settles, then walks forward, stops, walks back, stops, turns and walks
/// the other way. Returns the largest pose error after settling.
PoseError walkAround(Stage& Scene) {
    Rig& Body = *Scene.Bodies.front();
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Scene.step();
    PoseError Worst;
    const auto run = [&](float Speed, int Ticks) {
        for (int Tick = 0; Tick < Ticks; ++Tick) {
            Scene.step(Speed);
            const PoseError Error = Body.getPoseError();
            if (Error.Angle > Worst.Angle) Worst = Error;
        }
    };
    run(1.0f, 3 * TicksPerSecond / 2);
    run(0.0f, TicksPerSecond / 2);
    run(-1.0f, TicksPerSecond);
    run(0.0f, TicksPerSecond / 2);
    Body.setFacing(!Body.isFacingRight());
    run(0.0f, TicksPerSecond / 2);
    run(Body.isFacingRight() ? 1.0f : -1.0f, TicksPerSecond);
    run(0.0f, TicksPerSecond / 2);
    return Worst;
}

RigDef loadHumanoid() { return loadRigDef(DataDir / "rigs" / "humanoid.json"); }

/// A ball of \p Kg thrown at the torso of \p Body from the front at
/// \p Speed (m/s), as a strike would land.
physics::Body throwAtTorso(Stage& Scene, const Rig& Body, float Kg, float Speed) {
    const float Forward = Body.isFacingRight() ? 1.0f : -1.0f;
    const Vec2 Torso = Body.getPartPosition(BodyPart::Torso) + Vec2{0.0f, 0.1f};
    physics::Body Ball = Scene.PhysWorld.createBody(
        {.Position = Torso + Vec2{0.35f * Forward, 0.0f}, .Part = physics::PartRef{1, BodyPart::ForearmL}});
    Scene.PhysWorld.addShape(Ball, {.Kind = physics::ShapeKind::Circle, .Radius = 0.05f, .CollisionGroup = -2});
    Ball.setMass(Kg);
    Ball.setGravityScale(0.0f);
    Ball.setLinearVelocity({-Speed * Forward, 0.0f});
    return Ball;
}

} // namespace

TEST_CASE("Rig smooth body: walking, stopping and turning keep the upper body on the pose", "[rig][smooth]") {
    for (const Sheet& Fighter : getSheets()) {
        CAPTURE(Fighter.Name);
        Stage Scene(loadHumanoid());
        Rig& Body = Scene.add(makeSetup(Fighter, 0.0f, true, 0));
        const PoseError Worst = walkAround(Scene);
        CAPTURE(getBodyPartName(Worst.Part));
        CHECK(Worst.Angle < MaxWalkPoseError);
        CHECK(Body.isCarrying());
        CHECK(Body.getPosture() == Posture::Standing);
    }
}

TEST_CASE("Rig smooth body: the carrier transfer is what keeps the walk smooth", "[rig][smooth]") {
    // The same walk with the transfer and the feed-forward off: the upper
    // body swings like a passenger in a bus.
    const Sheet Heavy = getSheets()[2];
    RigDef Loose = loadHumanoid();
    Loose.Control.CarrierTransfer = 0.0f;
    Stage Swinging(Loose);
    Swinging.add(makeSetup(Heavy, 0.0f, true, 0));
    Stage Carried(loadHumanoid());
    Carried.add(makeSetup(Heavy, 0.0f, true, 0));
    CHECK(walkAround(Carried).Angle < walkAround(Swinging).Angle);
}

TEST_CASE("Rig smooth body: the feed-forward follows a moving pose closer", "[rig][smooth]") {
    // A jab played on the arms: with the clip's joint speed the joints lag
    // behind the clip less on average. (Its forearm extends faster than
    // maxJointSpeed, so neither follows it exactly.)
    const anim::Clip Jab = anim::loadClip(DataDir / "poses" / "jab.json");
    const auto playJab = [&](float FeedForward) {
        RigDef Def = loadHumanoid();
        Def.Control.FeedForward = FeedForward;
        Stage Scene(Def);
        Rig& Body = Scene.add(makeSetup(getSheets()[0], 0.0f, true, 0));
        for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Scene.step();
        float ErrorSum = 0.0f;
        int Steps = 0;
        for (float Time = 0.0f; Time < Jab.DurationSec; Time += Dt) {
            anim::Pose Shown = Scene.Stance;
            anim::layerPose(Shown, anim::sampleClip(Jab, Time));
            Body.setTargetAngles(Shown.Angles);
            Body.setBaseStiffness(Jab.Stiffness);
            Body.planMotion(Dt);
            Body.applyControl(Dt);
            Scene.PhysWorld.step(Dt);
            for (const auto Part : {BodyPart::Torso, BodyPart::UpperArmL, BodyPart::ForearmL}) {
                ErrorSum += std::abs(Shown.getAngle(Part) - Body.getJointAngle(Part));
            }
            ++Steps;
        }
        return ErrorSum / static_cast<float>(Steps);
    };
    CHECK(playJab(1.0f) < playJab(0.0f));
}

TEST_CASE("Rig smooth body: a hit still deflects the torso, which then recovers", "[rig][smooth]") {
    for (const Sheet& Fighter : getSheets()) {
        CAPTURE(Fighter.Name);
        Stage Scene(loadHumanoid());
        Rig& Body = Scene.add(makeSetup(Fighter, 0.0f, true, 0));
        for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Scene.step();
        const float Before = Body.getPoseError().Angle;

        // A heavy fist into the chest: the contact pushes the torso, the hit
        // softens the motors as combat would (applyHit without knockdown).
        throwAtTorso(Scene, Body, 3.0f, 8.0f);
        float Deflection = 0.0f;
        for (int Tick = 0; Tick < TicksPerSecond / 3; ++Tick) {
            Scene.step();
            if (Tick == 0) Body.applyHit(10.0f, {-1.0f, 0.0f}, Body.getPartPosition(BodyPart::Torso), false);
            Deflection = std::max(Deflection, Body.getPoseError().Angle);
        }
        CHECK(Deflection > Before + 0.05f);
        for (int Tick = 0; Tick < 2 * TicksPerSecond; ++Tick) Scene.step();
        CHECK(Body.getPoseError().Angle < MaxWalkPoseError);
    }
}

TEST_CASE("Rig smooth body: a knocked-down body is not carried and falls", "[rig][smooth]") {
    for (const Sheet& Fighter : getSheets()) {
        CAPTURE(Fighter.Name);
        Stage Scene(loadHumanoid());
        Rig& Body = Scene.add(makeSetup(Fighter, 0.0f, true, 0));
        for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Scene.step(1.0f);
        REQUIRE(Body.isCarrying());
        Body.applyHit(30.0f, {-1.0f, 0.0f}, Body.getPartPosition(BodyPart::Head), true);
        REQUIRE(Body.getPosture() == Posture::KnockedDown);
        float LowestHead = Body.getPartPosition(BodyPart::Head).Y;
        // Down until just before it gets up.
        const auto DownTicks = static_cast<int>(Scene.Def.Control.KnockdownSec / Dt) - 1;
        for (int Tick = 0; Tick < DownTicks; ++Tick) {
            Scene.step();
            CHECK_FALSE(Body.isCarrying());
            LowestHead = std::min(LowestHead, Body.getPartPosition(BodyPart::Head).Y);
        }
        CHECK(LowestHead < 0.6f);
    }
}

TEST_CASE("Rig smooth body: a guard that yielded stays tucked while the opponent is close", "[rig][smooth]") {
    // Two fighters in the stance so close that their guards press into the
    // opponent. A limb yields once and stays yielding; it does not come back
    // and jam again every fraction of a second.
    Stage Scene(loadHumanoid());
    const std::vector<Sheet> Sheets = getSheets();
    Rig& Left = Scene.add(makeSetup(Sheets[0], -0.3f, true, 0));
    Rig& Right = Scene.add(makeSetup(Sheets[1], 0.3f, false, 1));
    int Changes = 0;
    std::vector<bool> Was(BodyPartCount * 2, false);
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Scene.step();
        for (auto&& [Side, Body] : std::views::zip(std::views::iota(size_t{0}), std::array<Rig*, 2>{&Left, &Right})) {
            for (const auto Part : {BodyPart::UpperArmL, BodyPart::UpperArmR}) {
                const size_t Slot = Side * BodyPartCount + static_cast<size_t>(Part);
                const bool Now = Body->isYielding(Part);
                if (Now != Was[Slot]) ++Changes;
                Was[Slot] = Now;
            }
        }
    }
    // Each of the four guards may yield once (and return once), no more.
    CHECK(Changes <= 8);
    CHECK(Changes > 0);
}
