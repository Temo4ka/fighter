#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "anim/clip.hpp"
#include "combat/battle.hpp"
#include "combat/moves.hpp"
#include "combat/tuning.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

// Scenario tests of the fight of task 2.3 (docs/DEVELOPMENT_PLAN.md, agent D):
// reaction levels, blocks, stamina, chains, the end of the round. Tests that
// depend on the thresholds bring their own reaction table, so tuning
// data/reactions.json does not break them; the ones that check the shipped
// data say so.

namespace {

PlayerCommands press(MoveButton Button) {
    PlayerCommands Cmd;
    Cmd.Jab = Button == MoveButton::Jab;
    Cmd.HeavyPunch = Button == MoveButton::HeavyPunch;
    Cmd.BodyKick = Button == MoveButton::BodyKick;
    Cmd.LowKick = Button == MoveButton::LowKick;
    return Cmd;
}

bool isFree(const FighterView& View) { return View.State == FighterState::Idle || View.State == FighterState::Walking; }

/// How P1 attacks P2 in strike().
struct Attack {
    MoveButton Button = MoveButton::BodyKick;
    float Range = KickRange;
    /// Strike only at a victim that is free again (one reaction per strike);
    /// otherwise strike whenever P1 is free.
    bool WaitForVictim = true;
    PlayerCommands VictimCmd;
    /// Called after every update.
    std::function<void(const Battle&)> OnTick;
};

/// P1 walks up to \p How.Range and strikes P2 whenever it may, for \p Ticks.
/// Returns P1's strikes that landed.
std::vector<StrikeLanded> strike(Battle& Fight, const Attack& How, int Ticks) {
    std::vector<StrikeLanded> Landed;
    for (int Tick = 0; Tick < Ticks && !Fight.getResult(); ++Tick) {
        const FighterView& Left = getLeft(Fight);
        const FighterView& Right = getRight(Fight);
        PlayerCommands Cmd;
        const bool VictimReady = !How.WaitForVictim || Right.State == FighterState::Idle ||
                                 Right.State == FighterState::Walking || Right.State == FighterState::Blocking ||
                                 Right.State == FighterState::Crouching;
        if (Right.Position.X - Left.Position.X > How.Range) {
            Cmd.MoveX = 1.0f;
        } else if (isFree(Left) && VictimReady) {
            Cmd = press(How.Button);
        }
        Fight.update(Cmd, How.VictimCmd, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            const auto* Hit = std::get_if<StrikeLanded>(&Event);
            if (Hit && Hit->Contact.Attacker.Fighter == 0) Landed.push_back(*Hit);
        }
        if (How.OnTick) How.OnTick(Fight);
    }
    return Landed;
}

ReactionLevel getStrongest(const std::vector<StrikeLanded>& Hits) {
    ReactionLevel Strongest = ReactionLevel::None;
    for (const StrikeLanded& Hit : Hits) Strongest = std::max(Strongest, Hit.Reaction);
    return Strongest;
}

float getMeanStrength(const std::vector<StrikeLanded>& Hits) {
    float Sum = 0.0f;
    for (const StrikeLanded& Hit : Hits) Sum += Hit.Strength;
    return Hits.empty() ? 0.0f : Sum / static_cast<float>(Hits.size());
}

/// The ticks at which P1 started a move, with the move ids.
struct Started {
    int Tick = 0;
    std::string MoveId;
};

anim::Clip loadPose(const std::string& Name) {
    return anim::loadClip(std::filesystem::path(FIGHTER_DATA_DIR) / "poses" / (Name + ".json"));
}

template <class Event> size_t countEvents(const std::vector<BattleEvent>& Log) {
    return static_cast<size_t>(std::ranges::count_if(Log, [](const auto& Item) {
        return std::holds_alternative<Event>(Item);
    }));
}

} // namespace

TEST_CASE("Fight: a light hit is at most a flinch", "[combat][fight][data]") {
    // The shipped data/reactions.json: a jab touches or flinches, never more.
    Battle Fight(makeConfig());
    const std::vector<StrikeLanded> Hits =
        strike(Fight, {.Button = MoveButton::Jab, .Range = JabRange}, 6 * TicksPerSecond);
    REQUIRE(Hits.size() >= 3);
    for (const StrikeLanded& Hit : Hits) {
        CHECK(Hit.MoveId == "jab");
        CHECK(Hit.Reaction >= ReactionLevel::Touch);   // the jab's min_reaction
        CHECK(Hit.Reaction <= ReactionLevel::Flinch);
        CHECK(Hit.Damage > 0.0f);
    }
}

TEST_CASE("Fight: a series of light hits raises the reaction level", "[combat][fight]") {
    // A jab alone stays below Flinch; the buildup of a series lowers the
    // thresholds until jabs flinch. A jab that slips past the guard lands on
    // the torso with up to 0.6 m/s (arms pass arms, task 2.1).
    ReactionSpec Spec{.MinStrength = {0.01f, 0.7f, 1.4f, 2.0f, 1000.0f},
                      .BuildupPerStrength = 5.0f,
                      .BuildupDecayPerSec = 0.1f,
                      .ThresholdDrop = 0.5f};
    const Attack Jabs{.Button = MoveButton::Jab, .Range = JabRange, .WaitForVictim = false};

    ScratchData Data("series");
    Data.write("reactions.json", makeReactionsJson(Spec));
    Battle Fight(Data.makeConfig());
    const std::vector<StrikeLanded> Hits = strike(Fight, Jabs, 6 * TicksPerSecond);
    REQUIRE(Hits.size() >= 4);
    CHECK(Hits.front().Reaction == ReactionLevel::Touch);
    CHECK(getStrongest(Hits) >= ReactionLevel::Flinch);

    // Without the buildup the same series stays at Touch.
    Spec.ThresholdDrop = 0.0f;
    Data.write("reactions.json", makeReactionsJson(Spec));
    Battle Plain(Data.makeConfig());
    const std::vector<StrikeLanded> PlainHits = strike(Plain, Jabs, 6 * TicksPerSecond);
    REQUIRE(PlainHits.size() >= 4);
    CHECK(getStrongest(PlainHits) == ReactionLevel::Touch);
}

TEST_CASE("Fight: a heavy fighter reacts no stronger than a light one", "[combat][fight]") {
    // The same kicks at a light and a heavy fighter; no knockdowns, so that
    // both stay on their feet for the whole series.
    ScratchData Data("heavy");
    Data.write("reactions.json", makeReactionsJson({.MinStrength = {0.03f, 0.12f, 0.35f, 0.6f, 1000.0f}}));
    const auto kickAt = [&](const FighterConfig& Victim) {
        BattleConfig Config = Data.makeConfig();
        Config.Right = Victim;
        Battle Fight(Config);
        const std::vector<StrikeLanded> Hits = strike(Fight, {}, 6 * TicksPerSecond);
        REQUIRE(Hits.size() >= 3);
        return Hits;
    };

    SECTION("CON") {
        FighterConfig Heavy;
        Heavy.Stats.Constitution = 20;
        const std::vector<StrikeLanded> Light = kickAt(FighterConfig{});
        const std::vector<StrikeLanded> Tough = kickAt(Heavy);
        CHECK(getMeanStrength(Tough) < getMeanStrength(Light));
        CHECK(getStrongest(Tough) <= getStrongest(Light));
    }
    SECTION("the knight and the rogue") {
        const std::vector<StrikeLanded> Rogue = kickAt(loadFighter("rogue"));
        const std::vector<StrikeLanded> Knight = kickAt(loadFighter("knight"));
        CHECK(getMeanStrength(Knight) < getMeanStrength(Rogue));
        CHECK(getStrongest(Knight) <= getStrongest(Rogue));
    }
}

TEST_CASE("Fight: the reaction level does not drop during a reaction", "[combat][fight]") {
    // A kick staggers for 2 s; the jabs that follow are weaker (flinch) but
    // neither lower the level nor keep the stagger going.
    constexpr float StaggerSec = 2.0f;
    ScratchData Data("no_drop");
    Data.write("reactions.json", makeReactionsJson({.MinStrength = {0.01f, 0.03f, 0.4f, 5.0f, 1000.0f},
                                                    .StunSec = {0.0f, 0.15f, StaggerSec, 0.5f, 0.0f}}));
    Battle Fight(Data.makeConfig());

    // One kick that staggers.
    std::vector<StrikeLanded> Kick;
    for (int Tick = 0; Kick.empty() && Tick < 3 * TicksPerSecond; ++Tick) Kick = strike(Fight, {}, 1);
    REQUIRE(Kick.size() == 1);
    REQUIRE(Kick.front().Reaction == ReactionLevel::Stagger);
    REQUIRE(getRight(Fight).State == FighterState::Reacting);

    // Then jabs while the victim staggers.
    ReactionLevel Previous = ReactionLevel::Stagger;
    bool Dropped = false;
    std::optional<int> FreeTick;
    int Tick = 0;
    const auto Watch = [&](const Battle& Current) {
        ++Tick;
        const FighterView& Victim = getRight(Current);
        if (Victim.State != FighterState::Reacting) {
            if (!FreeTick) FreeTick = Tick;
            return;
        }
        if (FreeTick) return;
        Dropped = Dropped || Victim.Reaction < Previous;
        Previous = Victim.Reaction;
    };
    const std::vector<StrikeLanded> Jabs = strike(
        Fight, {.Button = MoveButton::Jab, .Range = JabRange, .WaitForVictim = false, .OnTick = Watch},
        3 * TicksPerSecond);

    const auto IsWeaker = [](const StrikeLanded& Hit) { return Hit.Reaction < ReactionLevel::Stagger; };
    REQUIRE(std::ranges::any_of(Jabs, IsWeaker));
    CHECK_FALSE(Dropped);
    // The stagger ends on time: the jabs' own stun is short.
    REQUIRE(FreeTick.has_value());
    CHECK(static_cast<float>(*FreeTick) / TicksPerSecond < StaggerSec + 0.3f);
}

TEST_CASE("Fight: a block in the right zone softens the hit", "[combat][fight][data]") {
    // Kicks at the torso: from close range, where the shin lands on it (from
    // kicking range the foot meets the pelvis, which a low block covers).
    const auto kickAt = [](const PlayerCommands& Guard) {
        Battle Fight(makeConfig());
        run(Fight, {}, Guard, 1);
        const std::vector<StrikeLanded> Hits =
            strike(Fight, {.Range = CloseKickRange, .VictimCmd = Guard}, 4 * TicksPerSecond);
        REQUIRE_FALSE(Hits.empty());
        return std::pair(Hits, getRight(Fight));
    };
    const auto [Clean, CleanView] = kickAt({});
    CHECK(CleanView.Hp < CleanView.MaxHp);
    REQUIRE(Clean.front().Contact.Victim.Part == BodyPart::Torso);
    const float CleanDamage = Clean.front().Damage;

    SECTION("mid covers the torso") {
        const auto [Hits, View] = kickAt({.Block = true});
        CHECK(View.State == FighterState::Blocking);
        CHECK(View.Block == BlockZone::Mid);
        for (const StrikeLanded& Hit : Hits) {
            CHECK(Hit.Blocked);
            CHECK(Hit.Reaction <= ReactionLevel::Touch);
            CHECK(Hit.Damage < CleanDamage * 0.5f);
        }
        // Blocking costs stamina.
        CHECK(View.Stamina < View.MaxStamina);
    }
    SECTION("high and low leave the torso open") {
        for (const PlayerCommands& Guard : {PlayerCommands{.Up = true, .Block = true},
                                            PlayerCommands{.Down = true, .Block = true}}) {
            const auto [Hits, View] = kickAt(Guard);
            CHECK(View.State == FighterState::Blocking);
            CHECK(View.Block == (Guard.Up ? BlockZone::High : BlockZone::Low));
            CHECK_FALSE(Hits.front().Blocked);
            CHECK(Hits.front().Reaction >= ReactionLevel::Flinch);   // the kick's min_reaction
        }
    }
}

TEST_CASE("Fight: the commands choose the free state", "[combat][fight]") {
    Battle Fight(makeConfig());
    run(Fight, {.Down = true}, {}, 2);
    CHECK(getLeft(Fight).State == FighterState::Crouching);
    run(Fight, {.Down = true, .Block = true}, {}, 2);
    CHECK(getLeft(Fight).State == FighterState::Blocking);
    CHECK(getLeft(Fight).Block == BlockZone::Low);
    // The block wins over an attack button.
    run(Fight, {.Up = true, .Block = true, .Jab = true}, {}, 2);
    CHECK(getLeft(Fight).State == FighterState::Blocking);
    CHECK(getLeft(Fight).Block == BlockZone::High);
    run(Fight, {}, {}, 2);
    CHECK(getLeft(Fight).State == FighterState::Idle);
}

TEST_CASE("Fight: stamina runs out, slows the fighter down and comes back", "[combat][fight]") {
    BattleConfig Config = makeConfig();
    Battle Fight(Config);
    const float MaxStamina = getLeft(Fight).MaxStamina;
    const float SlowScale = loadCombatTuning(Config.DataDir / "combat.json").ExhaustedSpeedScale;

    // P1 jabs the air until it is exhausted: holding the button repeats. (A
    // jab, since its startup floor does not limit its speed already.)
    std::vector<Started> Starts;
    std::vector<BattleEvent> Log;
    for (int Tick = 0; Tick < 20 * TicksPerSecond && countEvents<Exhausted>(Log) == 0; ++Tick) {
        Fight.update(press(MoveButton::Jab), {}, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            Log.push_back(Event);
            if (const auto* Start = std::get_if<StrikeStarted>(&Event)) Starts.push_back({Tick, Start->MoveId});
        }
    }
    REQUIRE(countEvents<Exhausted>(Log) == 1);
    CHECK(getLeft(Fight).Stamina == 0.0f);
    REQUIRE(Starts.size() >= 3);
    const int FreshPeriod = Starts[1].Tick - Starts[0].Tick;

    // Exhausted jabs are slower; the event is told only once.
    const int ExhaustedFrom = static_cast<int>(Starts.size());
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Fight.update(press(MoveButton::Jab), {}, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            Log.push_back(Event);
            if (const auto* Start = std::get_if<StrikeStarted>(&Event)) Starts.push_back({Tick, Start->MoveId});
        }
    }
    CHECK(countEvents<Exhausted>(Log) == 1);
    REQUIRE(Starts.size() >= static_cast<size_t>(ExhaustedFrom) + 2);
    const int SlowPeriod = Starts[ExhaustedFrom + 1].Tick - Starts[ExhaustedFrom].Tick;
    CHECK(static_cast<float>(SlowPeriod) == Approx(static_cast<float>(FreshPeriod) / SlowScale).margin(2.0f));

    // Resting brings it back.
    run(Fight, {}, {}, 10 * TicksPerSecond);
    CHECK(getLeft(Fight).Stamina == Approx(MaxStamina));
    CHECK(getLeft(Fight).Hp == getLeft(Fight).MaxHp);
}

TEST_CASE("Fight: a knockout ends the fight", "[combat][fight]") {
    BattleConfig Config = makeConfig();
    Config.Right.StartHp = 1.0f;
    Battle Fight(Config);
    std::vector<BattleEvent> Log;
    const std::vector<StrikeLanded> Hits =
        strike(Fight, {.OnTick = [&](const Battle& Current) {
                   std::ranges::copy(Current.getEvents(), std::back_inserter(Log));
               }},
               5 * TicksPerSecond);
    REQUIRE(Fight.getResult().has_value());
    REQUIRE(Hits.size() == 1);
    const BattleResult& Result = *Fight.getResult();
    CHECK(Result.End == BattleEnd::Knockout);
    CHECK(Result.WinnerSide == Winner::Left);
    CHECK(Result.Fighters[1].Hp == 0.0f);
    CHECK(Result.Fighters[1].Knockdowns == 0);   // a knockout is not a knockdown
    CHECK(getRight(Fight).State == FighterState::KnockedOut);
    REQUIRE(std::holds_alternative<BattleOver>(Log.back()));
    CHECK(std::get<BattleOver>(Log.back()).End == BattleEnd::Knockout);

    // The knocked-out fighter falls and stays down; nothing is told any more.
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Fight.update(press(MoveButton::BodyKick), {}, Dt);
        CHECK(Fight.getEvents().empty());
    }
    CHECK(isDown(getRight(Fight)));
    CHECK(getRight(Fight).State == FighterState::KnockedOut);
    CHECK(Fight.getResult()->Fighters[1].Hp == 0.0f);
}

TEST_CASE("Fight: when the time is up more HP wins", "[combat][fight]") {
    const auto finish = [](const BattleConfig& Config) {
        Battle Fight(Config);
        run(Fight, {}, {}, static_cast<int>(Config.RoundTimeSec * TicksPerSecond) + 1);
        REQUIRE(Fight.getResult().has_value());
        CHECK(Fight.getResult()->End == BattleEnd::TimeUp);
        return Fight.getResult()->WinnerSide;
    };
    BattleConfig Config = makeConfig();
    Config.RoundTimeSec = 0.5;
    CHECK(finish(Config) == Winner::Draw);   // both untouched

    // Absolute HP, not the share of the maximum: 60 of 180 beats 50 of 100.
    Config.Left.Stats.Constitution = 20;
    Config.Left.StartHp = 60.0f;
    Config.Right.StartHp = 50.0f;
    CHECK(finish(Config) == Winner::Left);
    Config.Right.StartHp = 70.0f;
    CHECK(finish(Config) == Winner::Right);
    Config.Right.StartHp = 60.0f;
    CHECK(finish(Config) == Winner::Draw);
}

TEST_CASE("Fight: a hit may be chained into the next strike", "[combat][fight]") {
    const anim::Clip Jab = loadPose("jab");
    const float JabSec = anim::getDurationAtRate(Jab, 1.0f);
    // Tap the jab button twice: the second press comes during the first jab.
    const auto tapTwice = [&](Battle& Fight) {
        std::vector<int> Starts;
        for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
            Fight.update({.Jab = Tick < 2 || (Tick >= 10 && Tick < 12)}, {}, Dt);
            for (const BattleEvent& Event : Fight.getEvents()) {
                if (std::holds_alternative<StrikeStarted>(Event)) Starts.push_back(Tick);
            }
        }
        return Starts;
    };

    SECTION("a jab that hits is cancelled into the next one") {
        Battle Fight(makeConfig());
        while (getRight(Fight).Position.X - getLeft(Fight).Position.X > JabRange) {
            Fight.update({.MoveX = 1.0f}, {}, Dt);
        }
        run(Fight, {}, {}, TicksPerSecond / 2);
        const std::vector<int> Starts = tapTwice(Fight);
        REQUIRE(Starts.size() == 2);
        CHECK(static_cast<float>(Starts[1] - Starts[0]) / TicksPerSecond < JabSec - 0.05f);
    }
    SECTION("a jab that misses is not") {
        Battle Fight(makeConfig());
        const std::vector<int> Starts = tapTwice(Fight);
        CHECK(Starts.size() == 1);   // the second press came too early to start a new jab
    }
}

TEST_CASE("Fight: a weapon brings its own move", "[combat][fight][data]") {
    const auto heavyMoveOf = [](const FighterConfig& Who) {
        BattleConfig Config = makeConfig();
        Config.Left = Who;
        Battle Fight(Config);
        Fight.update(press(MoveButton::HeavyPunch), {}, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            if (const auto* Start = std::get_if<StrikeStarted>(&Event)) return Start->MoveId;
        }
        return std::string();
    };
    CHECK(heavyMoveOf(FighterConfig{}) == "heavy_punch");
    CHECK(heavyMoveOf(loadFighter("rogue")) == "sword_slash");
    CHECK(heavyMoveOf(loadFighter("knight")) == "hammer_smash");
}

TEST_CASE("Fight: DEX speeds strikes up, but not past the startup floor", "[combat][fight][data]") {
    const std::vector<MoveDef> Moves = loadMoveSet(std::filesystem::path(FIGHTER_DATA_DIR) / "moves");
    const MoveDef& JabMove = *findMove(Moves, MoveButton::Jab, "");
    // Ticks of the startup and of the whole jab.
    const auto measure = [](int Dexterity) {
        BattleConfig Config = makeConfig();
        Config.Left.Stats.Dexterity = Dexterity;
        Battle Fight(Config);
        Fight.update({.Jab = true}, {}, Dt);
        int Startup = 1;
        int Total = 1;
        while (getLeft(Fight).State == FighterState::Attacking && Total < 2 * TicksPerSecond) {
            Startup += getLeft(Fight).Phase == AttackPhase::Startup ? 1 : 0;
            ++Total;
            Fight.update({}, {}, Dt);
        }
        return std::pair(Startup, Total);
    };
    const auto [SlowStartup, SlowTotal] = measure(0);
    const auto [FastStartup, FastTotal] = measure(20);
    CHECK(FastTotal < SlowTotal);
    CHECK(FastStartup <= SlowStartup);
    CHECK(static_cast<float>(FastStartup) / TicksPerSecond >= JabMove.MinStartupSec - static_cast<float>(Dt));
}

TEST_CASE("Fight: a fighter at the wall cannot retreat", "[combat][fight]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {}, 8 * TicksPerSecond);
    CHECK(getLeft(Fight).AgainstWall);
    CHECK_FALSE(getRight(Fight).AgainstWall);
    const float AtWall = getPelvisX(getLeft(Fight));
    run(Fight, {.MoveX = -1.0f}, {}, TicksPerSecond);
    CHECK(getPelvisX(getLeft(Fight)) == AtWall);
    CHECK(getLeft(Fight).State == FighterState::Idle);   // no walking on the spot
    run(Fight, {.MoveX = 1.0f}, {}, TicksPerSecond / 2);
    CHECK_FALSE(getLeft(Fight).AgainstWall);
}

TEST_CASE("Fight: a fighter on the floor is not hit", "[combat][fight]") {
    ScratchData Data("floor");
    Data.write("reactions.json", makeReactionsJson(makeKnockdownKicks()));
    Battle Fight(Data.makeConfig());
    bool HitOnFloor = false;
    bool Fell = false;
    bool WasDown = false;   // before the update: the hit that fells it counts
    const auto Watch = [&](const Battle& Current) {
        HitOnFloor = HitOnFloor || (WasDown && !getHits(Current).empty());
        WasDown = getRight(Current).State == FighterState::KnockedDown;
        Fell = Fell || WasDown;
    };
    // Kick whenever free, also at the fighter on the floor.
    strike(Fight, {.Range = KickRange, .WaitForVictim = false, .OnTick = Watch}, 4 * TicksPerSecond);
    CHECK(Fell);
    CHECK_FALSE(HitOnFloor);
}
