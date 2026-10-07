#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

#include "../combat/scenario.hpp"
#include "combat/battle.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

// Scenario tests of the body (task 2.1) through a whole battle without a
// window: what the player sees of the body in situations that only appear
// between two fighters (a clinch at the wall, close hits, knockdowns).

namespace {

constexpr double Dt = 1.0 / 60.0;
constexpr int TicksPerSecond = 60;
/// Distance between the fighters' floor points at which P1 jabs, m.
constexpr float JabRange = 0.72f;
/// Distance between the pelvises to which P1 presses in at the wall, m:
/// closer than the rig's closeRange, the guards touch.
constexpr float CloseJabGap = 0.66f;

BattleConfig makeConfig() {
    BattleConfig Config;
    Config.DataDir = FIGHTER_DATA_DIR;
    return Config;
}

const PartTransform& getPart(const FighterView& View, BodyPart Part) { return View.Parts[static_cast<size_t>(Part)]; }

/// The far end of the jabbing forearm (the fist), world coordinates. In the
/// reference pose a forearm hangs down.
Vec2 getFist(const FighterView& View) {
    const PartTransform& Forearm = getPart(View, BodyPart::ForearmL);
    return Forearm.Position + rotate({0.0f, -Forearm.Size.Y * 0.5f}, Forearm.Angle);
}

/// How far the fist is in front of the pelvis, along the facing, m.
float getReach(const FighterView& View) {
    const float Forward = View.FacingRight ? 1.0f : -1.0f;
    return (getFist(View).X - getPart(View, BodyPart::Pelvis).Position.X) * Forward;
}

float getGap(const Battle& Fight) {
    const auto& Fighters = Fight.getSnapshot().Fighters;
    return Fighters[1].Position.X - Fighters[0].Position.X;
}

float getPelvisGap(const Battle& Fight) {
    const auto& Fighters = Fight.getSnapshot().Fighters;
    return getPart(Fighters[1], BodyPart::Pelvis).Position.X - getPart(Fighters[0], BodyPart::Pelvis).Position.X;
}

bool hasLanded(const Battle& Fight, uint8_t Attacker) {
    return std::ranges::any_of(Fight.getEvents(), [&](const BattleEvent& Event) {
        const auto* Landed = std::get_if<StrikeLanded>(&Event);
        return Landed && Landed->Contact.Attacker.Fighter == Attacker;
    });
}

/// One jab of P1 with nothing else going on.
struct JabTrace {
    float Guard = 0.0f;          ///< Reach before the jab, m.
    float Longest = 0.0f;        ///< Largest reach during the jab, m.
    float PeakSpeed = 0.0f;      ///< Fastest forward motion of the fist relative to the pelvis, m/s.
    bool Landed = false;
};

JabTrace traceJab(Battle& Fight) {
    JabTrace Trace;
    Trace.Guard = getReach(Fight.getSnapshot().Fighters[0]);
    Trace.Longest = Trace.Guard;
    float Previous = Trace.Guard;
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) {
        Fight.update({.Light = Tick == 0}, {}, Dt);
        const float Reach = getReach(Fight.getSnapshot().Fighters[0]);
        Trace.Longest = std::max(Trace.Longest, Reach);
        Trace.PeakSpeed = std::max(Trace.PeakSpeed, (Reach - Previous) / static_cast<float>(Dt));
        Trace.Landed = Trace.Landed || hasLanded(Fight, 0);
        Previous = Reach;
    }
    return Trace;
}

/// P2 backs into the right wall and P1 follows it, then P1 presses in to
/// close range. With \p Exchange both jab at each other for 10 s meanwhile.
/// Both rest for a second at the end. Returns the closest the pelvises came
/// in the last second of the exchange (a jab at its very end may stagger
/// P1 back before the rest), m. Close range is measured between the
/// pelvises: the floor point lags behind the pelvis in a step, so a walk
/// that stops by the floor points may end with the guards pressed into each
/// other (0.5 m between the pelvises), where no jab can gather speed.
float pressToWall(Battle& Fight, bool Exchange) {
    for (int Tick = 0; Tick < 4 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = getGap(Fight) > JabRange ? 1.0f : 0.0f}, {.MoveX = 1.0f}, Dt);
    }
    float Closest = 1e9f;
    for (int Tick = 0; Tick < 10 * TicksPerSecond; ++Tick) {
        const PlayerCommands Left{.MoveX = getPelvisGap(Fight) > CloseJabGap ? 1.0f : 0.0f,
                                  .Light = Exchange && Tick % 25 < 3};
        const PlayerCommands Right{.Light = Exchange && Tick % 29 < 3};
        Fight.update(Left, Right, Dt);
        if (Tick >= 9 * TicksPerSecond) Closest = std::min(Closest, getPelvisGap(Fight));
    }
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) Fight.update({}, {}, Dt);
    return Closest;
}

} // namespace

TEST_CASE("Scenario: after 10 s of jabs at the wall a jab still extends fully", "[rig][scenario]") {
    // The reference: the same jab in the open, nobody in reach.
    Battle Open(makeConfig());
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Open.update({}, {}, Dt);
    const JabTrace Free = traceJab(Open);
    REQUIRE(Free.Longest - Free.Guard > 0.2f);

    // The same place without the exchange: P1 pressed in close to P2 at the
    // wall, its guard resting on P2's chest.
    Battle Quiet(makeConfig());
    pressToWall(Quiet, false);
    const float QuietGuard = std::min(getReach(Quiet.getSnapshot().Fighters[1]), Free.Guard);
    const JabTrace Fresh = traceJab(Quiet);

    // Found by the user: after 10 s of jabs at close range against a wall the
    // arms and the torsos used to jam, the jab came out 5 cm and twice as
    // slow, resting did not help and one step back did.
    Battle Fight(makeConfig());
    const float Closest = pressToWall(Fight, true);
    const auto& Fighters = Fight.getSnapshot().Fighters;
    // Still close: as close as P1 gets without the exchange. The legs keep
    // the stances apart (about 0.75 m between the pelvises), so P1 cannot
    // press in to the rig's closeRange any more; landed jabs do not drive
    // the fighters apart for good either.
    CHECK(Closest < getPelvisGap(Quiet) + 0.05f);
    // The arms are back in the guard, not stuck in the opponent: between
    // the guard resting on the opponent's chest (pressed back a few cm) and
    // the free one. Jammed, it was 10 cm short.
    const float LowestGuard = std::max(std::min(Fresh.Guard, QuietGuard) - 0.03f, Free.Guard - 0.07f);
    for (const auto& View : Fighters) {
        CHECK(getReach(View) > LowestGuard);
        CHECK(getReach(View) < Free.Guard + 0.03f);
    }
    // The jab is as fast as without the exchange and either lands or
    // extends as far as in the open.
    const JabTrace Close = traceJab(Fight);
    CHECK(Close.PeakSpeed > Fresh.PeakSpeed * 0.8f);
    CHECK(Close.PeakSpeed > Free.PeakSpeed * 0.5f);
    CHECK((Close.Landed || Close.Longest - Close.Guard > (Free.Longest - Free.Guard) * 0.8f));
}

namespace {

/// Distance between the floor points at which P1 stops walking and kicks,
/// m: the foot lands on the pelvis both in the open (the walk stops at
/// 0.86 m) and at the wall (at 0.87 m, after the long walk there). From
/// the walk's stops at 0.90 m at the wall (0.94 m in the open before the
/// walk stopped on both feet) the foot grazes the top of the front thigh on
/// its way up and the contact stop holds it there (see test::KickRange).
constexpr float KickRange = 0.88f;

bool isKnockedDown(const Battle& Fight, uint8_t Fighter) {
    return std::ranges::any_of(Fight.getEvents(), [&](const BattleEvent& Event) {
        const auto* Down = std::get_if<KnockedDown>(&Event);
        return Down && Down->Fighter == Fighter;
    });
}

/// The horizontal extent of a fighter's parts (their turned bounding
/// boxes), m.
std::pair<float, float> getExtentX(const FighterView& View) {
    float Low = 1e9f;
    float High = -1e9f;
    for (const auto& Part : View.Parts) {
        const float Half = std::abs(rotate({Part.Size.X * 0.5f, 0.0f}, Part.Angle).X) +
                           std::abs(rotate({0.0f, Part.Size.Y * 0.5f}, Part.Angle).X);
        Low = std::min(Low, Part.Position.X - Half);
        High = std::max(High, Part.Position.X + Half);
    }
    return {Low, High};
}

/// Since task 2.3 data/reactions.json decides about knockdowns, not the rig:
/// a battle with a reaction table in which a clean kick knocks down.
BattleConfig makeKnockdownConfig(const test::ScratchData& Data) {
    Data.write("reactions.json", test::makeReactionsJson(test::makeKnockdownKicks()));
    return Data.makeConfig();
}

/// P1 walks into kicking range of P2 and kicks once; returns the tick P2
/// went down, if it did.
std::optional<int> kickDown(Battle& Fight, int Ticks) {
    bool Kicked = false;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        const bool InRange = getGap(Fight) <= KickRange;
        Fight.update({.MoveX = InRange || Kicked ? 0.0f : 1.0f, .Kick = InRange && !Kicked}, {}, Dt);
        Kicked = Kicked || InRange;
        if (isKnockedDown(Fight, 1)) return Tick;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("Scenario: the kicker does not walk through the fighter it knocked down", "[rig][scenario]") {
    const test::ScratchData Data("rig_walk_through");
    Battle Fight(makeKnockdownConfig(Data));
    REQUIRE(kickDown(Fight, 4 * TicksPerSecond).has_value());
    // The kick ends over the falling body; then P1 walks forward while P2
    // lies: its legs stop short of the body.
    const auto IsDown = [&] { return Fight.getSnapshot().Fighters[1].State == FighterState::KnockedDown; };
    for (int Tick = 0; Tick < TicksPerSecond * 2 / 3; ++Tick) Fight.update({}, {}, Dt);
    int Walked = 0;
    for (; Walked < TicksPerSecond && IsDown(); ++Walked) {
        Fight.update({.MoveX = 1.0f}, {}, Dt);
        const auto& Fighters = Fight.getSnapshot().Fighters;
        const float Toes = std::max(getPart(Fighters[0], BodyPart::FootL).Position.X,
                                    getPart(Fighters[0], BodyPart::FootR).Position.X) + 0.12f;
        CHECK(Toes < getExtentX(Fighters[1]).first + 0.05f);
    }
    CHECK(Walked > TicksPerSecond / 3);
}

TEST_CASE("Scenario: knocked down at the wall the fighter stays in the arena and gets up", "[rig][scenario]") {
    const test::ScratchData Data("rig_wall_knockdown");
    Battle Fight(makeKnockdownConfig(Data));
    const float HalfWidth = Fight.getConfig().Arena.HalfWidthM;
    // P2 backs into the wall first.
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) Fight.update({}, {.MoveX = 1.0f}, Dt);
    REQUIRE(kickDown(Fight, 6 * TicksPerSecond).has_value());

    float Farthest = 0.0f;
    bool Stood = false;
    for (int Tick = 0; Tick < 4 * TicksPerSecond && !Stood; ++Tick) {
        Fight.update({}, {}, Dt);
        for (const auto& Part : Fight.getSnapshot().Fighters[1].Parts) Farthest = std::max(Farthest, Part.Position.X);
        Stood = std::ranges::any_of(Fight.getEvents(), [](const BattleEvent& Event) {
            return std::holds_alternative<GotUp>(Event);
        });
    }
    // The wall stops the body: every part's center stays at least the
    // thinnest part's radius inside (the snapshot has no shapes).
    CHECK(Farthest < HalfWidth - 0.03f);
    CHECK(Stood);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) Fight.update({}, {}, Dt);
    const FighterView& Fallen = Fight.getSnapshot().Fighters[1];
    CHECK(getPart(Fallen, BodyPart::Head).Position.Y > 1.4f);
    CHECK(Fallen.Position.X < HalfWidth);
}
