#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "combat/moves.hpp"
#include "combat/tuning.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

// Scenario tests of a posed strike that meets the opponent's posed parts: a
// kick into the legs or the pelvis stops at the contact instead of going
// through (Box2D does not collide two posed bodies), holds, and recovers
// from there. The arms collide too: a jab hits the raised guard.

namespace {

/// A little more than the stop depth: the posed parts are put back to it
/// exactly, the snapshot rounds nothing.
constexpr float DepthMargin = 1e-3f;
/// From this far (body origins, m) the body kick reaches the pelvis past the
/// front thigh; closer, its foot meets the thigh (0.95 m and closer), further
/// the torso.
constexpr float PelvisKickDistance = 0.99f;
/// From this close (body origins, m) the bodies settle as close as the
/// spacing lets them: the body kick meets the opponent's front thigh soon
/// after its startup.
constexpr float JamKickDistance = 0.75f;
/// From this close (body origins, m) the body kick's foot meets the front
/// thigh.
constexpr float ThighKickDistance = 0.8f;
/// The striking leg touches the opponent's posed parts when it is this close
/// to them, m: the spacing of the bodies takes a held contact out to
/// touching.
constexpr float TouchGap = 0.002f;

/// What one kick of P1 at a P2 standing still did.
struct KickLog {
    std::vector<StrikeLanded> Hits;
    float Deepest = 0.0f;          ///< Deepest overlap of P1's striking leg with P2's posed parts, m.
    float DeepestInStartup = 0.0f; ///< The same before the active phase, m.
    int TouchingTicks = 0;         ///< Ticks with the striking leg touching P2's posed parts (TouchGap).
    bool BackToStance = false;     ///< P1 is free again with its legs as before the kick.
    std::vector<RenderSnapshot> Snapshots;
};

/// A change of a data file (data/combat.json by default): the first
/// \p From becomes \p To.
struct TuningEdit {
    std::string From;
    std::string To;
    std::string File = "combat.json";
};

/// In a scratch data directory \p Name, P1 kicks once with \p Button at P2, both standing \p Distance apart (body
/// origins, m), and both rest for 2 s after that. Knockdowns are off, so
/// P2's legs stay posed.
KickLog kickOnce(const std::string& Name, MoveButton Button, float Distance,
                 const std::vector<TuningEdit>& Edits = {}) {
    ScratchData Data(Name);
    Data.write("reactions.json", makeReactionsJson(makeNoKnockdowns()));
    Data.replace("combat.json", "\"spawnDistance\": 2.4", std::format("\"spawnDistance\": {}", Distance));
    for (const TuningEdit& Edit : Edits) Data.replace(Edit.File, Edit.From, Edit.To);
    Battle Fight(Data.makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 2);
    const FighterView Before = getLeft(Fight);

    KickLog Log;
    for (int Tick = 0; Tick < 2 * TicksPerSecond; ++Tick) {
        PlayerCommands Cmd;
        pressMove(Cmd, Button, Tick == 0);
        Fight.update(Cmd, {}, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            if (const auto* Hit = std::get_if<StrikeLanded>(&Event)) Log.Hits.push_back(*Hit);
        }
        float Depth = 0.0f;
        float Gap = 1e9f;
        for (const auto Part : {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL}) {
            Depth = std::max(Depth, getPosedPenetration(Fight, 0, Part));
            Gap = std::min(Gap, getPosedGap(Fight, 0, Part));
        }
        Log.Deepest = std::max(Log.Deepest, Depth);
        if (getLeft(Fight).Phase == AttackPhase::Startup) {
            Log.DeepestInStartup = std::max(Log.DeepestInStartup, Depth);
        }
        Log.TouchingTicks += Gap <= TouchGap ? 1 : 0;
        Log.Snapshots.push_back(Fight.getSnapshot());
    }

    const FighterView& After = getLeft(Fight);
    Log.BackToStance = After.State == FighterState::Idle;
    for (const auto Part : {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL}) {
        const float Turn = getPart(After, Part).Angle - getPart(Before, Part).Angle;
        Log.BackToStance = Log.BackToStance && std::abs(Turn) < 0.05f;
    }
    return Log;
}

CombatTuning loadTuning() { return loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json"); }

float getStopDepth() { return loadTuning().ContactStopDepth; }

} // namespace

TEST_CASE("Contact: a body kick stops at the pelvis it hits", "[combat][contact]") {
    // The foot reaches the pelvis at the end of its swing, past the front
    // thigh.
    const KickLog Log = kickOnce("contact_body", MoveButton::BodyKick, PelvisKickDistance);
    REQUIRE(Log.Hits.size() == 1);
    CHECK(Log.Hits[0].MoveId == "body_kick");
    CHECK(Log.Hits[0].Contact.Victim.Part == BodyPart::Pelvis);
    // The foot stays at the contact and goes no deeper than the stop depth.
    CHECK(Log.Deepest > 0.0f);
    CHECK(Log.Deepest <= getStopDepth() + DepthMargin);
    CHECK(Log.BackToStance);
}

TEST_CASE("Contact: a kick stops at the leg it hits", "[combat][contact]") {
    const KickLog Log = kickOnce("contact_leg", MoveButton::BodyKick, ThighKickDistance);
    REQUIRE(Log.Hits.size() == 1);
    CHECK(Log.Hits[0].MoveId == "body_kick");
    const BodyPart Victim = Log.Hits[0].Contact.Victim.Part;
    CHECK((Victim == BodyPart::ThighL || Victim == BodyPart::ThighR));
    CHECK(Log.Deepest > 0.0f);
    CHECK(Log.Deepest <= getStopDepth() + DepthMargin);
    CHECK(Log.BackToStance);
}

TEST_CASE("Contact: the kick holds the contact pose and then recovers", "[combat][contact][slow]") {
    // The foot stays on the thigh while the attack holds the contact pose:
    // contactHoldSec (0.08 s), to within a tick. Then it goes back the way
    // it came (not on through the extended pose into the thigh). Without
    // knockback, so the kicked body does not move away from the foot.
    const KickLog Log = kickOnce("contact_hold", MoveButton::BodyKick, ThighKickDistance,
                                 {{"\"knockbackScale\": 1.0", "\"knockbackScale\": 0.0", "rigs/humanoid.json"}});
    CHECK(static_cast<float>(Log.TouchingTicks + 1) / TicksPerSecond >= loadTuning().ContactHoldSec);
    // The leg does not jump: the foot moves no more than a kick swings it
    // (about 5 cm per tick), also when it leaves the contact.
    float FastestFoot = 0.0f;
    for (size_t Index = 1; Index < Log.Snapshots.size(); ++Index) {
        const Vec2 Step = getPart(Log.Snapshots[Index].Fighters[0], BodyPart::FootL).Position -
                          getPart(Log.Snapshots[Index - 1].Fighters[0], BodyPart::FootL).Position;
        FastestFoot = std::max(FastestFoot, Step.getLength());
    }
    CHECK(FastestFoot < 0.12f);
}

TEST_CASE("Contact: the hit keeps the speed the leg came in at", "[combat][contact][slow]") {
    // The stop puts the leg back after the hit is measured: the same kick
    // with a stop that never bites lands just as hard.
    const KickLog Stopped = kickOnce("contact_stopped", MoveButton::BodyKick, ThighKickDistance);
    const KickLog Free = kickOnce("contact_free", MoveButton::BodyKick, ThighKickDistance,
                                  {{"\"contactStopDepth\": 0.01", "\"contactStopDepth\": 1.0"}});
    REQUIRE(Stopped.Hits.size() == 1);
    REQUIRE(Free.Hits.size() == 1);
    CHECK(Stopped.Hits[0].Contact.Impulse == Approx(Free.Hits[0].Contact.Impulse));
    CHECK(Stopped.Hits[0].Contact.ApproachSpeed > 0.5f);
}

TEST_CASE("Contact: a stopped kick is deterministic", "[combat][contact]") {
    const KickLog First = kickOnce("contact_first", MoveButton::BodyKick, PelvisKickDistance);
    const KickLog Second = kickOnce("contact_second", MoveButton::BodyKick, PelvisKickDistance);
    REQUIRE(First.Snapshots.size() == Second.Snapshots.size());
    for (size_t Index = 0; Index < First.Snapshots.size(); ++Index) {
        for (size_t Side = 0; Side < 2; ++Side) {
            const FighterView& Lhs = First.Snapshots[Index].Fighters[Side];
            const FighterView& Rhs = Second.Snapshots[Index].Fighters[Side];
            for (size_t Part = 0; Part < BodyPartCount; ++Part) {
                CHECK(Lhs.Parts[Part].Position == Rhs.Parts[Part].Position);
                CHECK(Lhs.Parts[Part].Angle == Rhs.Parts[Part].Angle);
            }
        }
    }
}

TEST_CASE("Contact: a kick that meets the opponent in its startup is jammed there", "[combat][contact]") {
    // So close, with its active phase moved later, the body kick meets P2's
    // legs before the active phase: it stops at them like in any other phase
    // (nothing passes through) and the attack recovers from there.
    const KickLog Jammed = kickOnce("contact_jammed", MoveButton::BodyKick, JamKickDistance,
                                    {{"\"active\": [0.18, 0.42]", "\"active\": [0.36, 0.42]", "poses/kick.json"}});
    CHECK(Jammed.DeepestInStartup > 0.0f);
    CHECK(Jammed.Deepest <= getStopDepth() + DepthMargin);
    CHECK(Jammed.BackToStance);
}

TEST_CASE("Contact: a jab hits the raised guard", "[combat][contact]") {
    // The arms collide (the rig's "passThrough" is empty): from where the
    // fist reaches P2, it meets P2's forearm first.
    ScratchData Data("guard");
    Data.replace("combat.json", "\"spawnDistance\": 2.4", std::format("\"spawnDistance\": {}", GuardJabRange));
    Battle Fight(Data.makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 2);
    std::vector<physics::HitEvent> Hits;
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        Fight.update({.Light = Tick == 0}, {}, Dt);
        for (const auto& Hit : getHits(Fight)) Hits.push_back(Hit);
    }
    REQUIRE(Hits.size() == 1);
    CHECK(Hits[0].Attacker.Part == BodyPart::ForearmL);
    CHECK((Hits[0].Victim.Part == BodyPart::ForearmL || Hits[0].Victim.Part == BodyPart::ForearmR));
}

