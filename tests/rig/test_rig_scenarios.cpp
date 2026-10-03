#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <variant>

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
        Fight.update({.Jab = Tick == 0}, {}, Dt);
        const float Reach = getReach(Fight.getSnapshot().Fighters[0]);
        Trace.Longest = std::max(Trace.Longest, Reach);
        Trace.PeakSpeed = std::max(Trace.PeakSpeed, (Reach - Previous) / static_cast<float>(Dt));
        Trace.Landed = Trace.Landed || hasLanded(Fight, 0);
        Previous = Reach;
    }
    return Trace;
}

} // namespace

TEST_CASE("Scenario: after 10 s of jabs at the wall a jab still extends fully", "[rig][scenario]") {
    // The reference: the same jab in the open, nobody in reach.
    Battle Open(makeConfig());
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) Open.update({}, {}, Dt);
    const JabTrace Free = traceJab(Open);
    REQUIRE(Free.Longest - Free.Guard > 0.2f);

    // P2 backs into the right wall, P1 follows into jab range and keeps
    // pressing while both jab for 10 s (found by the user: the arms and the
    // torsos used to jam, and the jab came out 5 cm and twice as slow).
    Battle Fight(makeConfig());
    for (int Tick = 0; Tick < 4 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = getGap(Fight) > JabRange ? 1.0f : 0.0f}, {.MoveX = 1.0f}, Dt);
    }
    for (int Tick = 0; Tick < 10 * TicksPerSecond; ++Tick) {
        const PlayerCommands Left{.MoveX = getGap(Fight) > 0.6f ? 1.0f : 0.0f, .Jab = Tick % 25 < 3};
        const PlayerCommands Right{.Jab = Tick % 29 < 3};
        Fight.update(Left, Right, Dt);
    }
    // Resting used to change nothing.
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) Fight.update({}, {}, Dt);

    // The arms are back in the guard, not stuck in the opponent.
    for (const auto& View : Fight.getSnapshot().Fighters) CHECK(getReach(View) == Approx(Free.Guard).margin(0.05f));
    // The jab starts as fast as in the open and either lands or extends fully.
    const JabTrace Close = traceJab(Fight);
    CHECK(Close.PeakSpeed > Free.PeakSpeed * 0.7f);
    CHECK((Close.Landed || Close.Longest - Close.Guard > (Free.Longest - Free.Guard) * 0.8f));
}
