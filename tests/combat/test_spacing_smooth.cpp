#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "combat/battle.hpp"
#include "debug/draw.hpp"
#include "scenario.hpp"

#if FIGHTER_DEBUG
#include "debug/draw_list.hpp"
#endif

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;

// The fighters meet in one smooth mechanism (rig/spacing.hpp): a walk into
// the opponent slows down, a push apart eases in, nothing is shoved back and
// forth between the steps, at the wall neither. Walking, the legs step as
// far as the pelvis goes: a walk held by the opponent does not slide its
// planted feet or march on the spot.

namespace {

/// No pelvis moves faster than this in the scenarios, m/s: the fastest walk
/// (the rogue, 1.33 m/s) with a margin. A shove of the old spacing reached
/// 9-12 m/s for a step.
constexpr float MaxPelvisSpeed = 1.6f;
/// A change of the pelvis velocity between two steps larger than this, with
/// the sign flipping, is a twitch back and forth, m/s.
constexpr float FlipStep = 0.3f;
/// Two walkers meeting may bounce back once (the heavier pushes the lighter
/// one); more flips are a twitch.
constexpr int MaxFlips = 1;
/// A foot lying flat this close to the floor stands on it, m, rad.
constexpr float FlatFootHeight = 0.005f;
constexpr float FlatFootAngle = 0.02f;
/// A planted foot of the walker slides no faster than this in one step, m/s
/// (a foot set down while the leg cannot reach it is dragged a step or
/// two)...
constexpr float MaxFootSlip = 1.5f;
/// ...and no farther than this over SlipWindow steps, m: a foot that slid
/// along with the pelvis would go the walk, 0.3 m in 0.25 s.
constexpr float MaxWindowSlip = 0.06f;
constexpr size_t SlipWindow = TicksPerSecond / 4;
/// The first stride from the stance slides a foot (a cross-over of the walk
/// clip, not the spacing): the slip is watched from then on.
constexpr int SlipFromTick = TicksPerSecond / 2;

float getSoleHeight(const FighterView& View, BodyPart Foot) {
    const PartTransform& Part = getPart(View, Foot);
    const float Drop = std::abs(std::sin(Part.Angle)) * Part.Size.X * 0.5f +
                       std::abs(std::cos(Part.Angle)) * Part.Size.Y * 0.5f;
    return Part.Position.Y - Drop;
}

bool isFlatOnFloor(const FighterView& View, BodyPart Foot) {
    return getSoleHeight(View, Foot) < FlatFootHeight && std::abs(getPart(View, Foot).Angle) < FlatFootAngle;
}

/// What one fighter did in a scenario.
struct MotionLog {
    float FastestPelvis = 0.0f;   ///< m/s.
    int Flips = 0;                ///< Velocity sign flips with a step larger than FlipStep.
    float FastestFootSlip = 0.0f; ///< A flat planted foot, from SlipFromTick on, m/s.
    float LongestSlide = 0.0f;    ///< The most the flat planted feet slid in SlipWindow steps, m.
    std::string Where;            ///< Where the worst happened, for the test output.
};

/// Runs \p Ticks with constant commands and watches both pelvises and feet.
std::array<MotionLog, 2> watch(Battle& Fight, const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd,
                               int Ticks) {
    std::array<MotionLog, 2> Logs;
    std::array<FighterView, 2> Before = {getLeft(Fight), getRight(Fight)};
    std::array<float, 2> LastVelocity{};
    std::array<std::vector<float>, 2> Slides;   // per step, m
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        Fight.update(LeftCmd, RightCmd, Dt);
        const std::array<FighterView, 2> Now = {getLeft(Fight), getRight(Fight)};
        for (size_t Side = 0; Side < 2; ++Side) {
            MotionLog& Log = Logs[Side];
            const float Velocity = (getPelvisX(Now[Side]) - getPelvisX(Before[Side])) / static_cast<float>(Dt);
            Log.FastestPelvis = std::max(Log.FastestPelvis, std::abs(Velocity));
            if (Velocity * LastVelocity[Side] < 0.0f && std::abs(Velocity - LastVelocity[Side]) > FlipStep) {
                ++Log.Flips;
                Log.Where += std::format(" flip at tick {} ({:+.2f} -> {:+.2f} m/s)", Tick, LastVelocity[Side], Velocity);
            }
            LastVelocity[Side] = Velocity;
            if (Tick < SlipFromTick) continue;
            float Slide = 0.0f;
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                if (!isFlatOnFloor(Now[Side], Foot) || !isFlatOnFloor(Before[Side], Foot)) continue;
                Slide = std::max(Slide, std::abs(getPart(Now[Side], Foot).Position.X -
                                                 getPart(Before[Side], Foot).Position.X));
            }
            Log.FastestFootSlip = std::max(Log.FastestFootSlip, Slide / static_cast<float>(Dt));
            Slides[Side].push_back(Slide);
            if (Slides[Side].size() >= SlipWindow) {
                float Window = 0.0f;
                for (size_t Index = Slides[Side].size() - SlipWindow; Index < Slides[Side].size(); ++Index) {
                    Window += Slides[Side][Index];
                }
                Log.LongestSlide = std::max(Log.LongestSlide, Window);
            }
        }
        Before = Now;
    }
    return Logs;
}

BattleConfig makeDuel(const std::string& Left, const std::string& Right, float ArenaHalfWidth) {
    BattleConfig Config = makeConfig();
    if (!Left.empty()) Config.Left = loadFighter(Left);
    if (!Right.empty()) Config.Right = loadFighter(Right);
    Config.Arena.HalfWidthM = ArenaHalfWidth;
    return Config;
}

void checkFeet(const MotionLog& Log) {
    CHECK(Log.FastestFootSlip < MaxFootSlip);
    CHECK(Log.LongestSlide < MaxWindowSlip);
}

void checkSmooth(const MotionLog& Log) {
    INFO(Log.Where);
    CHECK(Log.FastestPelvis <= MaxPelvisSpeed);
    CHECK(Log.Flips <= MaxFlips);
}

} // namespace

TEST_CASE("Spacing: walking into a fighter at the wall slows down without a jerk", "[combat][spacing][slow]") {
    // P2 stands with its back close to the wall (the arena is narrow), P1
    // walks into it and keeps pressing.
    Battle Fight(makeDuel("knight", "rogue", 1.6f));
    const auto Logs = watch(Fight, {.MoveX = 1.0f}, {}, 5 * TicksPerSecond);
    checkSmooth(Logs[0]);
    checkSmooth(Logs[1]);
    checkFeet(Logs[0]);
    CHECK(getRight(Fight).AgainstWall);
}

TEST_CASE("Spacing: both at the walls, the walker is stopped smoothly", "[combat][spacing]") {
    // The arena is so narrow that both stand at their walls when they meet.
    Battle Fight(makeDuel("knight", "rogue", 1.5f));
    const auto Logs = watch(Fight, {.MoveX = 1.0f}, {}, 4 * TicksPerSecond);
    checkSmooth(Logs[0]);
    checkSmooth(Logs[1]);
    checkFeet(Logs[0]);
}

TEST_CASE("Spacing: walking into each other and into a standing fighter has no twitch", "[combat][spacing][slow]") {
    SECTION("both walk in") {
        Battle Fight(makeDuel("knight", "rogue", 5.0f));
        const auto Logs = watch(Fight, {.MoveX = 1.0f}, {.MoveX = -1.0f}, 5 * TicksPerSecond);
        checkSmooth(Logs[0]);
        checkSmooth(Logs[1]);
        checkFeet(Logs[0]);
    }
    SECTION("the knight walks into the rogue") {
        Battle Fight(makeDuel("knight", "rogue", 5.0f));
        const auto Logs = watch(Fight, {.MoveX = 1.0f}, {}, 6 * TicksPerSecond);
        checkSmooth(Logs[0]);
        checkSmooth(Logs[1]);
        checkFeet(Logs[0]);
    }
    SECTION("a light fighter walks into a heavy one") {
        BattleConfig Config = makeConfig();
        Config.Right.Stats.Constitution = 20;
        Battle Fight(Config);
        const auto Logs = watch(Fight, {.MoveX = 1.0f}, {}, 5 * TicksPerSecond);
        checkSmooth(Logs[0]);
        checkSmooth(Logs[1]);
        checkFeet(Logs[0]);
    }
}

TEST_CASE("Spacing: holding forward from close range steps without sliding", "[combat][spacing][slow]") {
    // P1 starts right at P2 (their legs touch) and holds forward: it is held
    // or pushes P2, but its planted feet do not slide and its legs do not
    // march on the spot.
    for (const float Distance : {0.8f, 0.9f, 1.1f}) {
        INFO("spawn distance " << Distance);
        ScratchData Data(std::format("spacing_close_{}", static_cast<int>(Distance * 100.0f)));
        Data.replace("combat.json", "\"spawnDistance\": 2.4", std::format("\"spawnDistance\": {}", Distance));
        BattleConfig Config = Data.makeConfig();
        Config.Left = loadFighter("knight");
        Battle Fight(Config);
        const auto Logs = watch(Fight, {.MoveX = 1.0f}, {}, 3 * TicksPerSecond);
        checkSmooth(Logs[0]);
        checkSmooth(Logs[1]);
        checkFeet(Logs[0]);
    }
}

TEST_CASE("Spacing: a walk held by the opponent does not march on the spot", "[combat][spacing][slow]") {
    // The knight presses the rogue into the wall: the pelvis stops, and so do
    // the legs (the walk cycle follows the pelvis, not the key).
    Battle Fight(makeDuel("knight", "rogue", 1.6f));
    run(Fight, {.MoveX = 1.0f}, {}, 4 * TicksPerSecond);
    const FighterView Held = getLeft(Fight);
    run(Fight, {.MoveX = 1.0f}, {}, TicksPerSecond);
    const FighterView Later = getLeft(Fight);
    CHECK(std::abs(getPelvisX(Later) - getPelvisX(Held)) < 0.01f);
    for (const BodyPart Part : {BodyPart::ThighL, BodyPart::ThighR, BodyPart::FootL, BodyPart::FootR}) {
        CHECK(std::abs(getPart(Later, Part).Angle - getPart(Held, Part).Angle) < 0.02f);
    }
}

TEST_CASE("Spacing: the panel tells what moves each fighter", "[combat][spacing][slow]") {
#if FIGHTER_DEBUG
    const auto getLine = [](const std::string& Key) {
        for (const auto& [Name, Value] : debug::getDrawList().getPanel()) {
            if (Name == Key) return Value;
        }
        return std::string();
    };
    Battle Fight(makeDuel("knight", "rogue", 1.6f));
    bool SawSlowed = false;
    bool SawPushed = false;
    bool SawWall = false;
    bool SawHeld = false;
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = 1.0f}, {}, Dt);
        const std::string Walker = getLine("P1 push");
        const std::string Pushed = getLine("P2 push");
        REQUIRE(Walker.starts_with("spacing: slowed"));
        SawSlowed = SawSlowed || !Walker.starts_with("spacing: slowed +0.00");
        SawPushed = SawPushed || Pushed.find("pushed +0.00") == std::string::npos;
        SawWall = SawWall || Pushed.find("wall +0.00") == std::string::npos;
        SawHeld = SawHeld || getLine("P1 legs").find("held by the opponent") != std::string::npos;
    }
    CHECK(SawSlowed);
    CHECK(SawPushed);
    CHECK(SawWall);
    CHECK(SawHeld);
    // One line per contact stage (rig::ContactResolver).
    CHECK(getLine("contact spacing").find("needs") != std::string::npos);
    CHECK(getLine("contact walls").find("P2 right wall") != std::string::npos);
    CHECK(getLine("contact posed").find("passes") != std::string::npos);
    CHECK_FALSE(getLine("contact push-out").empty());
    CHECK_FALSE(getLine("overlap").empty());
#else
    SUCCEED("no panel in the release build");
#endif
}

TEST_CASE("Blends: a strike shows its own pose before its active phase", "[combat][blend]") {
    // A jab thrown from a walk blends from the walk pose, but the blend ends
    // within strikeStartupShare of the startup: the active phase starts at
    // the same tick as from standing, and the panel shows no blend then.
    const auto getActiveTick = [](bool FromWalk) {
        Battle Fight(makeConfig());
        if (FromWalk) run(Fight, {.MoveX = 1.0f}, {}, TicksPerSecond / 2);
        for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
            Fight.update({.MoveX = FromWalk ? 1.0f : 0.0f, .Light = Tick == 0}, {}, Dt);
            if (getLeft(Fight).Phase != AttackPhase::Active) continue;
#if FIGHTER_DEBUG
            for (const auto& [Name, Value] : debug::getDrawList().getPanel()) {
                if (Name == "P1 blend") CHECK(Value.starts_with("pose -"));
            }
#endif
            return Tick;
        }
        return -1;
    };
    const int Standing = getActiveTick(false);
    REQUIRE(Standing > 0);
    CHECK(getActiveTick(true) == Standing);
}

TEST_CASE("Spacing: a kick jammed on a fighter at the wall does not throw the attacker back", "[combat][spacing][slow]") {
    // P2 backs into the wall, P1 walks up to it and body-kicks from close
    // range: the kick jams on the thigh. Recovering, the leg used to swing
    // on into P2 (the clip's extended pose), and the spacing shoved P1 back
    // 0.37 m in two steps (12 m/s).
    Battle Fight(makeConfig());
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) Fight.update({}, {.MoveX = 1.0f}, Dt);
    bool Kicked = false;
    float Fastest = 0.0f;
    float Before = getPelvisX(getLeft(Fight));
    for (int Tick = 0; Tick < 7 * TicksPerSecond; ++Tick) {
        const float Gap = getPelvisX(getRight(Fight)) - getPelvisX(getLeft(Fight));
        const bool Close = Gap <= 0.66f;
        Fight.update({.MoveX = Close || Kicked ? 0.0f : 1.0f, .Kick = Close && !Kicked}, {}, Dt);
        Kicked = Kicked || Close;
        const float Now = getPelvisX(getLeft(Fight));
        Fastest = std::max(Fastest, static_cast<float>(std::abs(Now - Before) / Dt));
        Before = Now;
    }
    REQUIRE(Kicked);
    CHECK(Fastest <= MaxPelvisSpeed);
}
