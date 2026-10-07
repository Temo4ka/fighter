#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "combat/battle.hpp"
#include "combat/tuning.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;

// The global invariant of the body: no part of one fighter passes through
// any part of the other one, in any phase of anything they do. Long
// scripted fights (both attack, walk into each other, at the wall, crouch,
// block, chain, knock each other down) watch the overlap of two parts of
// different fighters every step: none may go deeper than its pair is allowed
// (Battle::findWorstOverlap, data/combat.json armOverlapTolerance and
// overlapTolerance).

namespace {

/// What a scripted fighter does for a while.
enum class Act : uint8_t {
    Approach, Retreat, Hold, Jab, Heavy, BodyKick, LowKick, Crouch, CrouchKick, BlockHigh, BlockMid, BlockLow, Chain,
    Count
};

/// A deterministic random sequence (a 32-bit LCG): the same seed gives the
/// same fight.
class Dice {
public:
    explicit Dice(uint32_t Seed) : State(Seed) {}
    uint32_t roll(uint32_t Sides) {
        State = State * 1664525u + 1013904223u;
        return (State >> 8) % Sides;
    }

private:
    uint32_t State;
};

/// One fighter driven by a script: a random act every 0.2-0.8 s, done
/// towards the opponent.
class Script {
public:
    Script(uint32_t Seed, bool IsPushy) : Roll(Seed), Pushy(IsPushy) {}

    PlayerCommands next(const FighterView& Self, const FighterView& Other) {
        if (LeftTicks <= 0) {
            Current = static_cast<Act>(Roll.roll(static_cast<uint32_t>(Act::Count)));
            if (Pushy && Current == Act::Retreat) Current = Act::Approach;
            LeftTicks = 12 + static_cast<int>(Roll.roll(36));
            Age = 0;
        }
        --LeftTicks;
        ++Age;
        const float Forward = getPart(Other, BodyPart::Pelvis).Position.X >
                                      getPart(Self, BodyPart::Pelvis).Position.X ? 1.0f : -1.0f;
        PlayerCommands Cmd;
        // A pushy fighter walks in whatever else it does (when it may).
        if (Pushy) Cmd.MoveX = Forward;
        switch (Current) {
            case Act::Approach: Cmd.MoveX = Forward; break;
            case Act::Retreat: Cmd.MoveX = -Forward; break;
            case Act::Hold: break;
            case Act::Jab: Cmd.Jab = Age % 20 < 2; break;
            case Act::Heavy: Cmd.HeavyPunch = Age == 1; break;
            case Act::BodyKick: Cmd.BodyKick = Age == 1; break;
            case Act::LowKick: Cmd.LowKick = Age == 1; break;
            case Act::Crouch: Cmd.Down = true; break;
            case Act::CrouchKick: Cmd.Down = true; Cmd.LowKick = Age == 6; break;
            case Act::BlockHigh: Cmd.Block = true; Cmd.Up = true; Cmd.MoveX = 0.0f; break;
            case Act::BlockMid: Cmd.Block = true; break;
            case Act::BlockLow: Cmd.Block = true; Cmd.Down = true; break;
            case Act::Chain: Cmd.Jab = Age % 12 < 2 && Age < 30; Cmd.HeavyPunch = Age == 30; break;
            case Act::Count: break;
        }
        return Cmd;
    }

private:
    Dice Roll;
    bool Pushy = false;
    Act Current = Act::Hold;
    int LeftTicks = 0;
    int Age = 0;
};

/// The worst overlap a fight reached (furthest beyond what its pair may
/// overlap, Battle::getOverlapTolerance), and where.
struct OverlapLog {
    float WorstExcess = -1.0f;   ///< Depth minus tolerance; positive: too deep.
    std::string Where;
    /// How many steps overlapped deeper than allowed, per pair of parts.
    std::map<std::string, int> DeepSteps;
    std::map<std::string, float> PairDeepest;

    void watch(const Battle& Fight) {
        const std::optional<physics::PartOverlap> Now = Fight.findWorstOverlap();
        if (!Now) return;
        const auto& Fighters = Fight.getSnapshot().Fighters;
        const std::string Pair = std::format("P{} {} / P{} {}", Now->First.Fighter + 1,
                                             getBodyPartName(Now->First.Part), Now->Second.Fighter + 1,
                                             getBodyPartName(Now->Second.Part));
        const float Excess = Now->Depth - Fight.getOverlapTolerance(*Now);
        if (Excess > 0.0f) {
            ++DeepSteps[Pair];
            PairDeepest[Pair] = std::max(PairDeepest[Pair], Now->Depth);
        }
        if (Excess <= WorstExcess) return;
        WorstExcess = Excess;
        Where = std::format("{:.3f} m {} at tick {} (P1 {} {}, P2 {} {}, pelvises {:.2f} m apart)", Now->Depth, Pair,
                            Fight.getSnapshot().Tick, static_cast<int>(Fighters[0].State), Fighters[0].MoveId,
                            static_cast<int>(Fighters[1].State), Fighters[1].MoveId,
                            std::abs(getPart(Fighters[1], BodyPart::Pelvis).Position.X -
                                     getPart(Fighters[0], BodyPart::Pelvis).Position.X));
    }

    /// Where it went too deep, for the test output.
    std::string describe() const {
        std::string Text = std::format("worst {}", Where);
        for (const auto& [Pair, Steps] : DeepSteps) {
            Text += std::format("\n  {}: {} steps too deep, deepest {:.3f}", Pair, Steps, PairDeepest.at(Pair));
        }
        return Text;
    }
};

/// Both fighters scripted for \p Seconds.
OverlapLog runScripted(Battle& Fight, uint32_t Seed, bool Pushy, int Seconds) {
    Script Left(Seed, Pushy);
    Script Right(Seed * 7919u + 13u, Pushy);
    OverlapLog Log;
    for (int Tick = 0; Tick < Seconds * TicksPerSecond && !Fight.getResult(); ++Tick) {
        const auto& Fighters = Fight.getSnapshot().Fighters;
        const PlayerCommands LeftCmd = Left.next(Fighters[0], Fighters[1]);
        const PlayerCommands RightCmd = Right.next(Fighters[1], Fighters[0]);
        Fight.update(LeftCmd, RightCmd, Dt);
        Log.watch(Fight);
    }
    return Log;
}

} // namespace

TEST_CASE("No pass-through: a scripted brawl in the open", "[combat][overlap]") {
    ScratchData Data("overlap_open");
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    Battle Fight(Data.makeConfig());
    const OverlapLog Log = runScripted(Fight, 1, false, 40);
    INFO(Log.describe());
    CHECK(Log.WorstExcess <= 0.0f);
}

TEST_CASE("No pass-through: both walk into each other and strike", "[combat][overlap]") {
    ScratchData Data("overlap_pushy");
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    Battle Fight(Data.makeConfig());
    const OverlapLog Log = runScripted(Fight, 2, true, 40);
    INFO(Log.describe());
    CHECK(Log.WorstExcess <= 0.0f);
}

TEST_CASE("No pass-through: at the wall", "[combat][overlap]") {
    ScratchData Data("overlap_wall");
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    Battle Fight(Data.makeConfig());
    OverlapLog Log;
    // P2 backs into the wall, P1 follows.
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = 1.0f}, {.MoveX = 1.0f}, Dt);
        Log.watch(Fight);
    }
    const OverlapLog Rest = runScripted(Fight, 3, true, 40);
    INFO(Log.describe());
    INFO(Rest.describe());
    CHECK(Log.WorstExcess <= 0.0f);
    CHECK(Rest.WorstExcess <= 0.0f);
}

TEST_CASE("No pass-through: the allowed overlap depends on the pair of parts", "[combat][overlap]") {
    ScratchData Data("overlap_tolerance");
    Data.replace("combat.json", "\"armOverlapTolerance\": 0.02", "\"armOverlapTolerance\": 0.03");
    // Spawned inside each other: the bodies overlap before the first step.
    Data.replace("combat.json", "\"spawnDistance\": 2.4", "\"spawnDistance\": 0.3");
    Battle Fight(Data.makeConfig());
    const physics::PartOverlap Arms{.First = {0, BodyPart::ForearmL}, .Second = {1, BodyPart::UpperArmR}};
    const physics::PartOverlap ArmOnLeg{.First = {0, BodyPart::ForearmL}, .Second = {1, BodyPart::ShinR}};
    CHECK(Fight.getOverlapTolerance(Arms) == 0.03f);
    CHECK(Fight.getOverlapTolerance(ArmOnLeg) == loadCombatTuning(Data.getDir() / "combat.json").OverlapTolerance);

    // The worst overlap is the one furthest beyond its tolerance, not
    // necessarily the deepest.
    const std::optional<physics::PartOverlap> Worst = Fight.findWorstOverlap();
    const std::optional<physics::PartOverlap> Deepest = Fight.findDeepestOverlap();
    REQUIRE(Worst.has_value());
    REQUIRE(Deepest.has_value());
    CHECK(Worst->Depth - Fight.getOverlapTolerance(*Worst) >= Deepest->Depth - Fight.getOverlapTolerance(*Deepest));

    // Far apart nothing overlaps.
    Battle Apart(makeConfig());
    CHECK_FALSE(Apart.findWorstOverlap().has_value());
}
