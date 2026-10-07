#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "combat/moves.hpp"
#include "combat/tuning.hpp"
#include "rig/rig_def.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

// Scenario tests of a fight without a window: scripted input -> expected
// state. They are the definition of done of the hybrid body (phase 1.5):
// the fighter stands and walks without balance assists, strikes land and
// sway the opponent, knockback depends on the mass, a strong hit knocks the
// fighter down and it gets up, and the simulation is deterministic.
//
// Since task 2.3 data/reactions.json decides about knockdowns, not the rig:
// the tests that need one (or none) bring their own reaction table. The
// fight itself (damage, reactions, blocks, stamina, the end) is tested in
// test_fight.cpp.

namespace {

rig::ControlParams loadControl() {
    return rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json").Control;
}

/// The far end of the forearm (the fist) of a fighter facing right, in world
/// coordinates. In the reference pose a forearm hangs down.
Vec2 getFist(const FighterView& View) {
    const PartTransform& Forearm = getPart(View, BodyPart::ForearmL);
    return Forearm.Position + rotate({0.0f, -Forearm.Size.Y * 0.5f}, Forearm.Angle);
}

/// The torso angle relative to the pelvis: how far the upper body leans.
float getTorsoLean(const FighterView& View) {
    return getPart(View, BodyPart::Torso).Angle - getPart(View, BodyPart::Pelvis).Angle;
}

/// What happened to the dummy (P2) while P1 attacked it.
struct AttackLog {
    std::vector<physics::HitEvent> Hits;
    std::optional<int> FirstHitTick;
    std::optional<int> DownTick;      ///< First tick with the dummy's head near the floor.
    float LargestSway = 0.0f;         ///< Largest change of the dummy's torso lean, rad.
};

/// P1 walks up to the dummy (P2) until \p Range, then presses \p Button
/// once every \p Period ticks, for \p Ticks.
AttackLog attackDummy(Battle& Fight, MoveButton Button, int Period, int Ticks, float Range) {
    AttackLog Log;
    const float RestLean = getTorsoLean(getRight(Fight));
    int AttackTick = 0;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        PlayerCommands LeftCmd;
        if (getRight(Fight).Position.X - getLeft(Fight).Position.X > Range && AttackTick == 0) {
            LeftCmd.MoveX = 1.0f;
        } else {
            const bool Press = AttackTick++ % Period < 3;
            LeftCmd.Jab = Press && Button == MoveButton::Jab;
            LeftCmd.BodyKick = Press && Button == MoveButton::BodyKick;
            LeftCmd.LowKick = Press && Button == MoveButton::LowKick;
        }
        Fight.update(LeftCmd, {}, Dt);

        std::ranges::copy(getHits(Fight), std::back_inserter(Log.Hits));
        if (!Log.Hits.empty() && !Log.FirstHitTick) Log.FirstHitTick = Tick;
        if (isDown(getRight(Fight)) && !Log.DownTick) Log.DownTick = Tick;
        Log.LargestSway = std::max(Log.LargestSway, std::abs(getTorsoLean(getRight(Fight)) - RestLean));
    }
    return Log;
}

float getTotalMass(const FighterConfig& Config) {
    const stats::PhysicalProfile Profile =
        stats::computeProfile(Config.Stats, Config.Loadout, stats::BalanceTable::getDefaults());
    float Sum = 0.0f;
    for (const auto& Part : Profile.Parts) Sum += Part.MassKg;
    return Sum;
}

} // namespace

TEST_CASE("Battle: fighters start on opposite sides facing each other", "[combat]") {
    Battle Fight(makeConfig());
    const FighterView& Left = getLeft(Fight);
    const FighterView& Right = getRight(Fight);
    CHECK(Left.Position.X < Right.Position.X);
    CHECK(Left.FacingRight);
    CHECK_FALSE(Right.FacingRight);
    CHECK(Left.Hp == Left.MaxHp);
    // Every body part is published for the renderer.
    CHECK(Left.Parts.size() == BodyPartCount);
    CHECK(Right.Parts.size() == BodyPartCount);
    // They start in the stance, standing on the floor.
    CHECK(isUpright(Left));
    CHECK(isUpright(Right));
    CHECK(Left.Position.Y == Approx(0.0f).margin(0.005f));
}

TEST_CASE("Battle: an idle fighter stands still for 60 s", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond);
    const float StartX = getPelvisX(getLeft(Fight));
    const float StartHead = getHeadHeight(getLeft(Fight));

    bool AlwaysUpright = true;
    float HeadDrift = 0.0f;
    for (int Tick = 0; Tick < 60 * TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        for (const auto& View : Fight.getSnapshot().Fighters) AlwaysUpright = AlwaysUpright && isUpright(View);
        HeadDrift = std::max(HeadDrift, std::abs(getHeadHeight(getLeft(Fight)) - StartHead));
    }
    CHECK(AlwaysUpright);
    // The pelvis is moved by the controller: standing still is exact.
    CHECK(getPelvisX(getLeft(Fight)) == StartX);
    CHECK(getLeft(Fight).Position.Y == Approx(0.0f).margin(0.005f));
    // The physical upper body settles and stays.
    CHECK(HeadDrift < 0.02f);
    CHECK(getHits(Fight).empty());
}

TEST_CASE("Battle: walking speed matches the data", "[combat][dod]") {
    const rig::ControlParams Control = loadControl();
    // P2 backs away so that P1 has room.
    const auto measureSpeed = [](Battle& Fight, float MoveX) {
        run(Fight, {.MoveX = MoveX}, {.MoveX = 1.0f}, TicksPerSecond / 2);   // accelerate
        const float StartX = getPelvisX(getLeft(Fight));
        run(Fight, {.MoveX = MoveX}, {.MoveX = 1.0f}, TicksPerSecond);
        return getPelvisX(getLeft(Fight)) - StartX;
    };

    SECTION("forward and backward") {
        Battle Forward(makeConfig());
        CHECK(measureSpeed(Forward, 1.0f) == Approx(Control.WalkSpeed).epsilon(0.01));
        CHECK(isUpright(getLeft(Forward)));
        Battle Backward(makeConfig());
        CHECK(measureSpeed(Backward, -1.0f) == Approx(-Control.WalkSpeed * Control.BackwardSpeedScale).epsilon(0.01));
    }
    SECTION("DEX makes a fighter faster") {
        BattleConfig Config = makeConfig();
        Config.Left.Stats.Dexterity = 20;
        const float Scale =
            stats::computeProfile(Config.Left.Stats, {}, stats::BalanceTable::getDefaults()).MoveSpeedScale;
        REQUIRE(Scale > 1.1f);
        Battle Nimble(Config);
        CHECK(measureSpeed(Nimble, 1.0f) == Approx(Control.WalkSpeed * Scale).epsilon(0.01));
    }
}

TEST_CASE("Battle: releasing the key stops the fighter", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {}, 2 * TicksPerSecond);
    run(Fight, {}, {}, TicksPerSecond / 2);   // decelerates, the step finishes

    const float StoppedX = getPelvisX(getLeft(Fight));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getPelvisX(getLeft(Fight)) == StoppedX);
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: walking stops at the arena wall", "[combat]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {.MoveX = 1.0f}, 10 * TicksPerSecond);
    const float HalfWidth = Fight.getConfig().Arena.HalfWidthM;
    const float BodyHalfWidth = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json").BodyHalfWidth;
    CHECK(getPelvisX(getLeft(Fight)) == Approx(-HalfWidth + BodyHalfWidth));
    CHECK(getPelvisX(getRight(Fight)) == Approx(HalfWidth - BodyHalfWidth));
    for (const auto& View : Fight.getSnapshot().Fighters) {
        CHECK(std::abs(View.Position.X) < HalfWidth);
        CHECK(isUpright(View));
    }
}

TEST_CASE("Battle: fighters do not pass through each other", "[combat][dod]") {
    // Walking into each other, the fighters meet and stay there: the
    // pelvises never closer than the pushboxes (their legs usually meet
    // first), and once met, neither gets past the other.
    const float BodyHalfWidth = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json").BodyHalfWidth;
    const auto getGap = [](const Battle& Fight) { return getPelvisX(getRight(Fight)) - getPelvisX(getLeft(Fight)); };
    Battle Fight(makeConfig());
    float SmallestGap = 10.0f;
    for (int Tick = 0; Tick < 4 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = 1.0f}, {.MoveX = -1.0f}, Dt);
        SmallestGap = std::min(SmallestGap, getGap(Fight));
    }
    const float MetGap = getGap(Fight);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = 1.0f}, {.MoveX = -1.0f}, Dt);
        SmallestGap = std::min(SmallestGap, getGap(Fight));
    }
    CHECK(SmallestGap >= 2.0f * BodyHalfWidth - 1e-4f);
    CHECK(getGap(Fight) == Approx(MetGap).margin(0.005f));
    CHECK(getPelvisX(getLeft(Fight)) < getPelvisX(getRight(Fight)));

    // Walking into a standing fighter pushes it, never closer than the
    // pushboxes.
    BattleConfig Config = makeConfig();
    Config.Right.Stats.Constitution = 20;
    Battle Push(Config);
    const float HeavyStartX = getPelvisX(getRight(Push));
    float Narrowest = 10.0f;
    for (int Tick = 0; Tick < 4 * TicksPerSecond; ++Tick) {
        Push.update({.MoveX = 1.0f}, {}, Dt);
        Narrowest = std::min(Narrowest, getGap(Push));
    }
    CHECK(getPelvisX(getRight(Push)) - HeavyStartX > 0.1f);
    CHECK(Narrowest >= 2.0f * BodyHalfWidth - 1e-4f);
}

TEST_CASE("Battle: a jab moves the fist forward", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 2);
    // Reach: how far the fist is in front of the pelvis.
    const auto getReach = [&] { return getFist(getLeft(Fight)).X - getPelvisX(getLeft(Fight)); };
    const float GuardReach = getReach();

    float LongestReach = GuardReach;
    Fight.update({.Jab = true}, {}, Dt);
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) {
        Fight.update({}, {}, Dt);
        LongestReach = std::max(LongestReach, getReach());
    }
    CHECK(LongestReach - GuardReach > 0.15f);
    // The jab is over and the fist is back in the guard.
    run(Fight, {}, {}, TicksPerSecond / 2);
    CHECK(getReach() == Approx(GuardReach).margin(0.1f));
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a kick raises the front foot forward", "[combat][dod]") {
    Battle Fight(makeConfig());
    const Vec2 Start = getPart(getLeft(Fight), BodyPart::FootL).Position;

    Vec2 Highest = Start;
    Fight.update({.BodyKick = true}, {}, Dt);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        const Vec2 Foot = getPart(getLeft(Fight), BodyPart::FootL).Position;
        if (Foot.Y > Highest.Y) Highest = Foot;
    }
    CHECK(Highest.Y > 0.9f);   // body height: the torso is the physical target
    CHECK(Highest.X > Start.X + 0.3f);
    // The standing foot stays on the floor all along.
    CHECK(getLeft(Fight).Position.Y == Approx(0.0f).margin(0.005f));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a jab at the dummy is a hit that sways it", "[combat][dod]") {
    // Close, from the stance: the fist lands while the arm still extends,
    // so the hit is strong enough to see the sway (at full reach it is a
    // tap). The fighters start there instead of walking up: a walk rests
    // with the feet wide (the layered walk), its front foot meets the
    // dummy's and the spacing parts them to full reach.
    constexpr float CloseJabRange = 0.72f;
    constexpr float CloseJabSpawn = 0.65f;   // m between the pelvises
    ScratchData Data("jab_dummy");
    Data.replace("combat.json", "\"spawnDistance\": 2.4", std::format("\"spawnDistance\": {}", CloseJabSpawn));
    Battle Fight(Data.makeConfig());
    const AttackLog Log =
        attackDummy(Fight, MoveButton::Jab, TicksPerSecond * 2 / 3, 4 * TicksPerSecond, CloseJabRange);

    REQUIRE_FALSE(Log.Hits.empty());
    for (const auto& Hit : Log.Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        CHECK(Hit.Attacker.Part == BodyPart::ForearmL);
        CHECK(Hit.Victim.Fighter == 1);
        CHECK(Hit.Impulse > 0.0f);
        CHECK(Hit.ApproachSpeed > 0.0f);
    }
    // The physical upper body of the dummy reacts, but a jab does not knock
    // it down.
    CHECK(Log.LargestSway > 0.05f);
    CHECK_FALSE(Log.DownTick.has_value());
    CHECK(isUpright(getLeft(Fight)));
    CHECK(isUpright(getRight(Fight)));
}

TEST_CASE("Battle: a kick at the dummy is a hit that sways and pushes it", "[combat][dod]") {
    // Without knockdowns, to see the plain reaction.
    ScratchData Data("kick");
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    Battle Fight(Data.makeConfig());
    const float DummyStartX = getPelvisX(getRight(Fight));

    const AttackLog Log = attackDummy(Fight, MoveButton::BodyKick, 2 * TicksPerSecond, 4 * TicksPerSecond, KickRange);
    REQUIRE_FALSE(Log.Hits.empty());
    float Strongest = 0.0f;
    for (const auto& Hit : Log.Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        // The kicking leg: the left one, or the right one when the walk
        // stopped with the right foot in front (the kick plays mirrored).
        CHECK((Hit.Attacker.Part == BodyPart::FootL || Hit.Attacker.Part == BodyPart::ShinL ||
               Hit.Attacker.Part == BodyPart::FootR || Hit.Attacker.Part == BodyPart::ShinR));
        Strongest = std::max(Strongest, Hit.Impulse);
    }
    // A kick lands harder than a jab; the dummy sways and is pushed back.
    CHECK(Strongest > 20.0f);
    CHECK(Log.LargestSway > 0.1f);
    CHECK(getPelvisX(getRight(Fight)) > DummyStartX + 0.05f);
    CHECK(isUpright(getRight(Fight)));
}

TEST_CASE("Battle: a heavy fighter is knocked back less than a light one", "[combat][dod]") {
    // Knockdowns off: both must stay standing for the knockback to compare.
    ScratchData Data("knockback");
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    const auto measureKnockback = [&](const FighterConfig& Dummy) {
        BattleConfig Config = Data.makeConfig();
        Config.Right = Dummy;
        Battle Fight(Config);
        // One kick; the dummy only moves when it is hit.
        const float StartX = getPelvisX(getRight(Fight));
        const AttackLog Log =
            attackDummy(Fight, MoveButton::BodyKick, 10 * TicksPerSecond, 3 * TicksPerSecond, KickRange);
        REQUIRE(Log.FirstHitTick.has_value());
        return getPelvisX(getRight(Fight)) - StartX;
    };

    // The knight: more CON and heavy armor; the rogue: less of both.
    const FighterConfig Knight = loadFighter("knight");
    const FighterConfig Rogue = loadFighter("rogue");
    REQUIRE(getTotalMass(Knight) > getTotalMass(Rogue) * 1.3f);
    const float HeavyDistance = measureKnockback(Knight);
    const float LightDistance = measureKnockback(Rogue);
    CHECK(LightDistance > 0.1f);
    CHECK(HeavyDistance > 0.0f);
    CHECK(HeavyDistance < LightDistance * 0.85f);

    // CON alone: the same body, only heavier.
    FighterConfig Tough;
    Tough.Stats.Constitution = 20;
    CHECK(measureKnockback(Tough) < measureKnockback(FighterConfig{}) * 0.9f);
}

TEST_CASE("Battle: a strong kick knocks the fighter down and it gets up", "[combat][dod]") {
    const rig::ControlParams Control = loadControl();
    // The reaction table makes a clean kick a knockdown.
    ScratchData Data("knockdown");
    Data.write("reactions.json", makeReactionsJson(makeKnockdownKicks()));
    Battle Fight(Data.makeConfig());
    // One kick, then nothing: the dummy falls, lies and gets up. A low kick:
    // it sweeps the legs, so the body falls at once. (The body kick lands
    // on the pelvis, close to the center of mass, which hardly turns the
    // body: it sinks to its knees first and lies after about 1.1 s.)
    const int Ticks = 6 * TicksPerSecond;
    const AttackLog Log = attackDummy(Fight, MoveButton::LowKick, Ticks, Ticks, KickRange);

    REQUIRE(Log.FirstHitTick.has_value());
    REQUIRE(Log.DownTick.has_value());
    // It falls and lies on the floor before it starts to get up. (With the
    // smooth body's limp legs it sinks to its knees first, then lies: about
    // 1.1-1.4 s, no longer within 1 s.)
    CHECK(*Log.DownTick - *Log.FirstHitTick < Control.KnockdownSec * TicksPerSecond);
    CHECK(isUpright(getRight(Fight)));
    CHECK(isUpright(getLeft(Fight)));

    // It is up again within the configured time on the floor and getting up
    // (plus a little for the upper body to straighten), and stands on its
    // feet when getting up is over (the feet come down from where they lay
    // while the body straightens).
    Battle Again(Data.makeConfig());
    std::optional<int> UpTick;
    std::optional<int> HitTick;
    bool GotUp = false;
    for (int Tick = 0; Tick < Ticks && !(UpTick && GotUp); ++Tick) {
        const bool InRange = getRight(Again).Position.X - getLeft(Again).Position.X <= KickRange;
        Again.update({.MoveX = HitTick || InRange ? 0.0f : 1.0f, .LowKick = InRange && !HitTick}, {}, Dt);
        if (!HitTick && !getHits(Again).empty()) HitTick = Tick;
        if (!UpTick && HitTick && Tick > *HitTick + TicksPerSecond && isUpright(getRight(Again))) UpTick = Tick;
        GotUp = GotUp || std::ranges::any_of(Again.getEvents(), [](const BattleEvent& Event) {
                    return std::holds_alternative<combat::GotUp>(Event);
                });
    }
    REQUIRE(UpTick.has_value());
    const float UpSec = static_cast<float>(*UpTick - *HitTick) / TicksPerSecond;
    CHECK(UpSec > Control.KnockdownSec);
    CHECK(UpSec < Control.KnockdownSec + Control.GetUpSec + 0.5f);
    REQUIRE(GotUp);
    CHECK(getRight(Again).Position.Y == Approx(0.0f).margin(0.005f));   // on its feet
}

TEST_CASE("Battle: same input gives the same result", "[combat][dod]") {
    // Low thresholds, so that the scenario has every reaction level: the
    // body kick on the pelvis (0.5-0.6 m/s) knocks down.
    ScratchData Data("determinism");
    Data.write("reactions.json", makeReactionsJson({.MinStrength = {0.02f, 0.05f, 0.2f, 0.35f, 0.5f},
                                                    .BuildupPerStrength = 1.0f,
                                                    .ThresholdDrop = 0.1f}));
    Battle First(Data.makeConfig());
    Battle Second(Data.makeConfig());
    // A little farther than KickRange: the body kick lands with the foot on
    // the pelvis instead of meeting the front thigh (the posed legs collide
    // and the foot stops there; the arms collide, so P2's guard stands
    // differently than when they passed each other). P1 kicks from where
    // its walk stopped (the legs settle into the stance, the kick steps into
    // its pose); the series knocks a fighter down at 0.98 to 1.04 m but not
    // at 0.94 m or closer; farther the kicks meet the torso or the guard.
    constexpr float PelvisKickRange = 1.0f;
    size_t HitCount = 0;
    bool KnockedDown = false;
    bool Kicked = false;   // in this period
    for (int Tick = 0; Tick < 10 * TicksPerSecond; ++Tick) {
        // Both battles get the same input, computed from the first one.
        const float Distance = getRight(First).Position.X - getLeft(First).Position.X;
        // P1 walks into kicking range and kicks once every 2.5 s (as soon as
        // it is in range in the first 0.5 s of the period), jabbing in
        // between. The kick is an input of the scenario, computed from the
        // first battle like the rest: a kick pressed only in the first 3
        // ticks of the period missed whenever P1 arrived a tick late.
        const int Phase = Tick % 150;
        if (Phase == 0) Kicked = false;
        const bool Kick = !Kicked && Distance <= PelvisKickRange && Phase < 30 &&
                          getLeft(First).State != FighterState::Attacking;
        Kicked = Kicked || Kick;
        const PlayerCommands LeftCmd{
            .MoveX = Distance > PelvisKickRange ? 1.0f : 0.0f,
            .Jab = Phase > 40 && Phase < 120 && Tick % 37 == 0,
            .BodyKick = Kick,
        };
        // P2 backs away now and then, but stands still while P1 kicks: a
        // kick that meets the legs at close range is weak (legs collide).
        const PlayerCommands RightCmd{.MoveX = (Tick / 70) % 3 == 1 ? 1.0f : 0.0f, .Jab = Tick % 53 == 0};
        First.update(LeftCmd, RightCmd, Dt);
        Second.update(LeftCmd, RightCmd, Dt);
        REQUIRE(getHits(First).size() == getHits(Second).size());
        HitCount += getHits(First).size();
        KnockedDown = KnockedDown || isDown(getRight(First)) || isDown(getLeft(First));
    }
    for (auto&& [Lhs, Rhs] : std::views::zip(First.getSnapshot().Fighters, Second.getSnapshot().Fighters)) {
        CHECK(Lhs.Position == Rhs.Position);
        CHECK(Lhs.Hp == Rhs.Hp);
        CHECK(Lhs.Stamina == Rhs.Stamina);
        CHECK(Lhs.State == Rhs.State);
        for (auto&& [PartL, PartR] : std::views::zip(Lhs.Parts, Rhs.Parts)) {
            CHECK(PartL.Position == PartR.Position);
            CHECK(PartL.Angle == PartR.Angle);
        }
    }
    // The scenario is only meaningful if the fighters actually hit each other.
    CHECK(HitCount > 0);
    CHECK(KnockedDown);
}

TEST_CASE("Battle: tuning is read from the data directory", "[combat][dod]") {
    ScratchData Data("tuning");
    BattleConfig Config = makeConfig();
    Config.DataDir = Data.getDir();

    SECTION("an edited value takes effect in a new battle") {
        const Battle Original(Config);
        const float DefaultGap = getRight(Original).Position.X - getLeft(Original).Position.X;
        Data.replace("combat.json", "\"spawnDistance\": 2.4", "\"spawnDistance\": 3.4");
        const Battle Edited(Config);
        CHECK(getRight(Edited).Position.X - getLeft(Edited).Position.X == Approx(DefaultGap + 1.0f).margin(0.01f));
    }
    SECTION("a broken file is reported, not ignored") {
        Data.write("combat.json", "{ \"spawnDistance\": ");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
    SECTION("a misspelled key is reported") {
        Data.write("combat.json", R"({ "spawnDistanse": 3.0 })");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
    SECTION("a broken reaction table is reported") {
        Data.replace("reactions.json", "\"damage_per_strength\"", "\"damage_per_strenght\"");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
    SECTION("a broken move is reported") {
        Data.replace("moves/jab.json", "\"Jab\"", "\"Uppercut\"");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
    SECTION("a missing clip without a stand-in is reported") {
        std::filesystem::remove(Data.getDir() / "poses" / "walk.json");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
}

TEST_CASE("Battle: result is available when round time runs out", "[combat]") {
    BattleConfig Config = makeConfig();
    Config.RoundTimeSec = 1.0;
    Battle Fight(Config);
    CHECK_FALSE(Fight.getResult().has_value());
    run(Fight, {}, {}, TicksPerSecond + 1);
    REQUIRE(Fight.getResult().has_value());
    CHECK(Fight.getResult()->TimeSec == Approx(1.0).margin(Dt));
}

// Phase 2 contracts (tasks 2.0.1, 2.0.4): what the battle reports through
// events, the snapshot and the result.

TEST_CASE("Battle: a knockdown kick is told by events and the result", "[combat][events]") {
    ScratchData Data("events");
    Data.write("reactions.json", makeReactionsJson(makeKnockdownKicks()));
    BattleConfig Config = Data.makeConfig();
    Config.RoundTimeSec = 6.0;
    Battle Fight(Config);

    std::vector<BattleEvent> Log;
    bool Kicked = false;
    while (!Fight.getResult()) {
        const bool InRange = getRight(Fight).Position.X - getLeft(Fight).Position.X <= KickRange;
        Fight.update({.MoveX = Kicked || InRange ? 0.0f : 1.0f, .BodyKick = InRange && !Kicked}, {}, Dt);
        Kicked = Kicked || InRange;
        std::ranges::copy(Fight.getEvents(), std::back_inserter(Log));
    }

    // In order: the kick starts, lands, the dummy falls and gets up, the time runs out.
    const auto findEvent = [&]<class Event>(std::type_identity<Event>, size_t From) {
        for (size_t Index = From; Index < Log.size(); ++Index) {
            if (std::holds_alternative<Event>(Log[Index])) return Index;
        }
        return Log.size();
    };
    const size_t Started = findEvent(std::type_identity<StrikeStarted>{}, 0);
    const size_t Landed = findEvent(std::type_identity<StrikeLanded>{}, Started);
    const size_t Down = findEvent(std::type_identity<KnockedDown>{}, Landed);
    const size_t Up = findEvent(std::type_identity<GotUp>{}, Down);
    REQUIRE(Up < Log.size());
    CHECK(std::get<StrikeStarted>(Log[Started]).Fighter == 0);
    CHECK(std::get<StrikeStarted>(Log[Started]).MoveId == "body_kick");
    const StrikeLanded& Hit = std::get<StrikeLanded>(Log[Landed]);
    CHECK(Hit.Contact.Victim.Fighter == 1);
    CHECK(Hit.MoveId == "body_kick");
    CHECK(Hit.Strength > 0.0f);
    CHECK(Hit.Damage > 0.0f);
    CHECK(Hit.Reaction == ReactionLevel::Knockdown);
    CHECK_FALSE(Hit.Blocked);
    CHECK(std::get<KnockedDown>(Log[Down]).Fighter == 1);
    CHECK(std::get<GotUp>(Log[Up]).Fighter == 1);
    REQUIRE(std::holds_alternative<BattleOver>(Log.back()));
    CHECK(std::get<BattleOver>(Log.back()).End == BattleEnd::TimeUp);

    const BattleResult& Result = *Fight.getResult();
    CHECK(Result.End == BattleEnd::TimeUp);
    CHECK(Result.TimeSec == Approx(Config.RoundTimeSec).margin(Dt));
    CHECK(Result.WinnerSide == Winner::Left);   // the kick took HP
    const StrikeStats& Kick = Result.Fighters[0].Moves.at("body_kick");
    CHECK(Kick.Thrown == 1);
    CHECK(Kick.Landed == 1);
    CHECK(Kick.Blocked == 0);
    CHECK(Kick.Damage == Approx(Hit.Damage));
    CHECK(Result.Fighters[0].DamageDealt == Approx(Hit.Damage));
    CHECK(Result.Fighters[1].DamageTaken == Approx(Hit.Damage));
    CHECK(Result.Fighters[0].DamageTaken == 0.0f);
    uint32_t HitsTaken = 0;
    for (const PartReport& Part : Result.Fighters[1].HitsTaken) HitsTaken += Part.Hits;
    CHECK(HitsTaken == 1);
    const PartReport& HitPart = Result.Fighters[1].HitsTaken[static_cast<size_t>(Hit.Contact.Victim.Part)];
    CHECK(HitPart.Hits == 1);
    CHECK(HitPart.Damage == Approx(Hit.Damage));
    CHECK(Result.Fighters[1].Hp == Approx(getRight(Fight).MaxHp - Hit.Damage));
    CHECK(Result.Fighters[1].Knockdowns == 1);
    CHECK(Result.Fighters[0].Knockdowns == 0);
    CHECK(Result.Fighters[0].Hp == getLeft(Fight).Hp);

    // After the end nothing happens any more.
    Fight.update({}, {}, Dt);
    CHECK(Fight.getEvents().empty());
}

TEST_CASE("Battle: starting HP comes from the config", "[combat][config]") {
    BattleConfig Config = makeConfig();
    Config.Left.StartHp = 30.0f;
    Config.Right.StartHp = 1.0e6f;
    const Battle Fight(Config);
    CHECK(getLeft(Fight).Hp == 30.0f);
    CHECK(getRight(Fight).Hp == getRight(Fight).MaxHp);   // clamped
    CHECK(getRight(Fight).MaxHp > 30.0f);
}

TEST_CASE("Battle: the snapshot shows the phases of an attack", "[combat][snapshot]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 4);
    CHECK(getLeft(Fight).State == FighterState::Idle);
    CHECK(getLeft(Fight).MoveId.empty());

    Fight.update({.Jab = true}, {}, Dt);
    std::vector<AttackPhase> Phases;
    for (int Tick = 0; Tick < TicksPerSecond && getLeft(Fight).State == FighterState::Attacking; ++Tick) {
        CHECK(getLeft(Fight).MoveId == "jab");
        if (Phases.empty() || Phases.back() != getLeft(Fight).Phase) Phases.push_back(getLeft(Fight).Phase);
        Fight.update({}, {}, Dt);
    }
    CHECK(Phases == std::vector{AttackPhase::Startup, AttackPhase::Active, AttackPhase::Recovery});
    CHECK(getLeft(Fight).State == FighterState::Idle);
    CHECK(getLeft(Fight).Phase == AttackPhase::None);

    Fight.update({.MoveX = 1.0f}, {}, Dt);
    CHECK(getLeft(Fight).State == FighterState::Walking);
}
