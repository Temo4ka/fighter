#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "combat/battle.hpp"
#include "combat/tuning.hpp"
#include "rig/rig_def.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;

// The stride follows the press: short, choppy presses of the walk key give
// short steps that match the pelvis travel, a release brings the legs back
// into the stance, a foot left far from the pose steps again, and the knees
// and the pelvis never sink to absorb a mismatch of legs and body.

namespace {

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
/// A sole this close to the floor stands on it, m.
constexpr float LandedHeight = 0.005f;
/// The pelvis may sink this much below the stance height, m: the walk's own
/// wide steps lower it 1-2 cm, and a step that begins with the standing
/// foot far back (left there by the step before) is wider still for a
/// while (up to about 4.5 cm). Before the stride work choppy taps sank it
/// by 10 cm.
constexpr float MaxPelvisSink = 0.05f;
/// A knee bends at most this much (the walk clip's deepest knee is 70 deg)
/// and never backwards more than a hair, rad.
constexpr float MaxKneeBend = 95.0f * RadiansPerDegree;
constexpr float MaxKneeBackward = 3.0f * RadiansPerDegree;
/// A planted ankle moves less than this in a step, m (the rig's foot IK
/// settles a few millimetres when a foot plants)...
constexpr float MaxPlantedSlipPerTick = 0.005f;
/// ...and less than this over the whole run, m.
constexpr float MaxPlantedSlipTotal = 0.05f;
/// Between two presses the feet move (both together, along the floor) at
/// most this many times the pelvis travel plus a little: a full step of the
/// walk clip moves the swing foot about 1.2 m for 0.48 m of travel (2.5x),
/// the planted one not at all; the even swing (swingEvenness 0.9) keeps it
/// within about 2.8x everywhere in the step, m/m and m. (Before the stride
/// work a 4 cm tap swung the feet 0.76 m.)
constexpr float MaxFootPerTravel = 3.0f;
constexpr float FootMotionSlack = 0.06f;
/// The feet's midpoint stays this close to under the pelvis, m: choppy taps
/// both ways put it up to 0.17 m off. (When the stride scaled with the DEX
/// walk speed, the rogue's choppy taps forward left it 0.29 m behind, the
/// body leaning over the feet.)
constexpr float MaxFeetOffCenter = 0.2f;
/// Rested after the release, the feet are this close to the stance's
/// spread and midpoint (from the pelvis), m.
constexpr float StanceTolerance = 0.015f;

const rig::RigDef& getRigDef() {
    static const rig::RigDef Def = loadHumanoid();
    return Def;
}

float getSoleHeight(const FighterView& View, BodyPart Foot) {
    const PartTransform& Part = getPart(View, Foot);
    const float Drop = std::abs(std::sin(Part.Angle)) * Part.Size.X * 0.5f +
                       std::abs(std::cos(Part.Angle)) * Part.Size.Y * 0.5f;
    return Part.Position.Y - Drop;
}

/// The ankle hinge of \p Foot along the arena, m (the end of the shin).
float getAnkleX(const FighterView& View, BodyPart Foot) {
    const BodyPart Shin = Foot == BodyPart::FootL ? BodyPart::ShinL : BodyPart::ShinR;
    const rig::PartDef& Shape = getRigDef().getPart(Shin);
    const float Center = (std::min(Shape.Begin.Y, Shape.End.Y) + std::max(Shape.Begin.Y, Shape.End.Y)) * 0.5f;
    float AnchorY = 0.0f;
    for (const auto& Joint : getRigDef().Joints) {
        if (Joint.Child == Foot) AnchorY = Joint.Anchor.Y;
    }
    const PartTransform& Part = getPart(View, Shin);
    return Part.Position.X - std::sin(Part.Angle) * (AnchorY - Center);
}

/// The knee angle, as for a fighter facing right (bent is negative), rad.
float getKnee(const FighterView& View, BodyPart Shin) {
    const BodyPart Thigh = Shin == BodyPart::ShinL ? BodyPart::ThighL : BodyPart::ThighR;
    const float Facing = View.FacingRight ? 1.0f : -1.0f;
    return (getPart(View, Shin).Angle - getPart(View, Thigh).Angle) * Facing;
}

/// The midpoint of the ankles from the pelvis along the facing, m.
float getFeetOffCenter(const FighterView& View) {
    const float FeetX = (getAnkleX(View, BodyPart::FootL) + getAnkleX(View, BodyPart::FootR)) * 0.5f;
    return (FeetX - getPelvisX(View)) * (View.FacingRight ? 1.0f : -1.0f);
}

/// How the fighter stands in the stance: its spread and feet's midpoint.
struct StanceShape {
    float PelvisHeight = 0.0f;
    float Spread = 0.0f;
    float OffCenter = 0.0f;
};

StanceShape measureStance(const FighterView& View) {
    return {.PelvisHeight = getPart(View, BodyPart::Pelvis).Position.Y,
            .Spread = std::abs(getAnkleX(View, BodyPart::FootL) - getAnkleX(View, BodyPart::FootR)),
            .OffCenter = getFeetOffCenter(View)};
}

/// A deterministic sequence of taps: pressed for 2-6 ticks, released for
/// 2-10, either way.
struct Tap {
    int Pressed = 0;
    int Released = 0;
    float MoveX = 0.0f;
};

std::vector<Tap> makeTaps(uint32_t Seed, int Count) {
    uint32_t State = Seed;
    const auto roll = [&](uint32_t Sides) {
        State = State * 1664525u + 1013904223u;
        return static_cast<int>((State >> 8) % Sides);
    };
    std::vector<Tap> Taps;
    for (int Index = 0; Index < Count; ++Index) {
        Taps.push_back({.Pressed = 2 + roll(5), .Released = 2 + roll(9), .MoveX = roll(2) == 0 ? 1.0f : -1.0f});
    }
    return Taps;
}

/// What P1's body did over a scripted walk.
struct StrideLog {
    float LowestPelvis = 1e9f;         ///< m.
    float DeepestKnee = 0.0f;          ///< Most bent knee, rad (negative).
    float MostBackwardKnee = 0.0f;     ///< Most hyperextended knee, rad (positive).
    float WorstSlipPerTick = 0.0f;     ///< A planted ankle in one step, m.
    float TotalSlip = 0.0f;            ///< All planted ankle motion, m.
    /// The same from the first release on (a long walk's own foot lock
    /// lets a planted foot drag a little; that is not the stride's).
    float ReleasedSlipPerTick = 0.0f;
    float ReleasedSlip = 0.0f;
    float WorstFootExcess = -1e9f;     ///< Feet motion beyond MaxFootPerTravel * travel + slack, m.
    float FeetOffCenter = 0.0f;        ///< The feet's midpoint from under the pelvis, m.
    /// At the end (rested): the ankles apart, and their midpoint from the
    /// pelvis along the facing, m.
    float EndSpread = 0.0f;
    float EndOffCenter = 0.0f;
    std::string Where;
};

/// P1 presses and releases MoveX as \p Taps say (then rests for 1 s); P2
/// stands. Watches P1 every tick.
StrideLog runTaps(Battle& Fight, const std::vector<Tap>& Taps) {
    StrideLog Log;
    FighterView Before = getLeft(Fight);
    int Tick = 0;
    float PelvisPath = 0.0f;
    float FootPath = 0.0f;
    bool Pressed = false;
    bool Released = false;
    const auto closeWindow = [&] {
        const float Excess = FootPath - (MaxFootPerTravel * PelvisPath + FootMotionSlack);
        if (Excess > Log.WorstFootExcess) {
            Log.WorstFootExcess = Excess;
            Log.Where += std::format(" [tick {}: feet {:.3f} m for pelvis {:.3f} m]", Tick, FootPath, PelvisPath);
        }
        PelvisPath = 0.0f;
        FootPath = 0.0f;
    };
    const auto step = [&](float MoveX) {
        const bool Press = MoveX != 0.0f;
        if (Press && !Pressed) closeWindow();
        Released = Released || (!Press && Pressed);
        Pressed = Press;
        Fight.update({.MoveX = MoveX}, {}, Dt);
        ++Tick;
        const FighterView& Now = getLeft(Fight);
        const float Travel = std::abs(getPelvisX(Now) - getPelvisX(Before));
        PelvisPath += Travel;
        Log.LowestPelvis = std::min(Log.LowestPelvis, getPart(Now, BodyPart::Pelvis).Position.Y);
        const float FeetX = (getAnkleX(Now, BodyPart::FootL) + getAnkleX(Now, BodyPart::FootR)) * 0.5f;
        Log.FeetOffCenter = std::max(Log.FeetOffCenter, std::abs(FeetX - getPelvisX(Now)));
        for (const BodyPart Shin : {BodyPart::ShinL, BodyPart::ShinR}) {
            Log.DeepestKnee = std::min(Log.DeepestKnee, getKnee(Now, Shin));
            Log.MostBackwardKnee = std::max(Log.MostBackwardKnee, getKnee(Now, Shin));
        }
        for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
            const float Move = std::abs(getAnkleX(Now, Foot) - getAnkleX(Before, Foot));
            FootPath += Move;
            if (getSoleHeight(Now, Foot) < LandedHeight && getSoleHeight(Before, Foot) < LandedHeight) {
                Log.WorstSlipPerTick = std::max(Log.WorstSlipPerTick, Move);
                Log.TotalSlip += Move;
                if (Released) {
                    Log.ReleasedSlipPerTick = std::max(Log.ReleasedSlipPerTick, Move);
                    Log.ReleasedSlip += Move;
                }
            }
        }
        Before = Now;
    };
    for (const Tap& Each : Taps) {
        for (int Index = 0; Index < Each.Pressed; ++Index) step(Each.MoveX);
        for (int Index = 0; Index < Each.Released; ++Index) step(0.0f);
    }
    for (int Index = 0; Index < TicksPerSecond; ++Index) step(0.0f);
    closeWindow();
    const FighterView& End = getLeft(Fight);
    Log.EndSpread = std::abs(getAnkleX(End, BodyPart::FootL) - getAnkleX(End, BodyPart::FootR));
    Log.EndOffCenter = getFeetOffCenter(End);
    return Log;
}

/// \p WholeRun: the slip is checked all through (taps), else from the
/// release on (a long walk).
void checkStride(const StrideLog& Log, const StanceShape& Stance, bool WholeRun) {
    const float StanceHeight = Stance.PelvisHeight;
    INFO(std::format("pelvis {:.3f} (stance {:.3f}), knee {:.1f}/{:.1f} deg, slip {:.4f}/tick {:.3f} total "
                     "(released {:.4f}/tick {:.3f}), feet excess {:.3f}, feet off center {:.3f}, "
                     "end spread {:.3f} (stance {:.3f}), end off center {:.3f} (stance {:.3f})",
                     Log.LowestPelvis, StanceHeight, Log.DeepestKnee / RadiansPerDegree,
                     Log.MostBackwardKnee / RadiansPerDegree, Log.WorstSlipPerTick, Log.TotalSlip,
                     Log.ReleasedSlipPerTick, Log.ReleasedSlip, Log.WorstFootExcess, Log.FeetOffCenter, Log.EndSpread, Stance.Spread,
                     Log.EndOffCenter, Stance.OffCenter));
    INFO(Log.Where);
    CHECK(Log.LowestPelvis >= StanceHeight - MaxPelvisSink);
    CHECK(Log.DeepestKnee >= -MaxKneeBend);
    CHECK(Log.MostBackwardKnee <= MaxKneeBackward);
    CHECK((WholeRun ? Log.WorstSlipPerTick : Log.ReleasedSlipPerTick) < MaxPlantedSlipPerTick);
    CHECK((WholeRun ? Log.TotalSlip : Log.ReleasedSlip) < MaxPlantedSlipTotal);
    CHECK(Log.WorstFootExcess <= 0.0f);
    CHECK(Log.FeetOffCenter <= MaxFeetOffCenter);
    CHECK(std::abs(Log.EndSpread - Stance.Spread) <= StanceTolerance);
    CHECK(std::abs(Log.EndOffCenter - Stance.OffCenter) <= StanceTolerance);
}

} // namespace

TEST_CASE("Stride: choppy taps both ways step short, with no squat, slide or big swing", "[combat][stride]") {
    for (const char* Name : {"knight", "rogue"}) {
        for (const uint32_t Seed : {1u, 2u, 3u}) {
            INFO(Name << " seed " << Seed);
            BattleConfig Config = makeConfig();
            Config.Left = loadFighter(Name);
            Battle Fight(Config);
            run(Fight, {}, {}, TicksPerSecond / 4);
            checkStride(runTaps(Fight, makeTaps(Seed, 40)), measureStance(getLeft(Fight)), true);
        }
    }
}

TEST_CASE("Stride: holding the key walks full steps, a release comes back into the stance", "[combat][stride]") {
    for (const char* Name : {"knight", "rogue"}) {
        for (const float MoveX : {1.0f, -1.0f}) {
            for (int Held = 30; Held < 80; Held += 7) {
                INFO(Name << " MoveX " << MoveX << " held " << Held);
                BattleConfig Config = makeConfig();
                Config.Left = loadFighter(Name);
                Battle Fight(Config);
                run(Fight, {}, {}, TicksPerSecond / 4);
                checkStride(runTaps(Fight, {{.Pressed = Held, .Released = 0, .MoveX = MoveX}}),
                            measureStance(getLeft(Fight)), false);
            }
        }
    }
}

TEST_CASE("Stride: choppy taps forward keep the feet under the body, whatever the walk speed", "[combat][stride]") {
    for (const char* Name : {"knight", "rogue"}) {
        for (const uint32_t Seed : {1u, 7u}) {
            INFO(Name << " seed " << Seed);
            BattleConfig Config = makeConfig();
            Config.Left = loadFighter(Name);
            Battle Fight(Config);
            run(Fight, {}, {}, TicksPerSecond / 4);
            const float StanceHeight = getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y;
            std::vector<Tap> Taps = makeTaps(Seed, 30);
            for (Tap& Each : Taps) Each.MoveX = 1.0f;
            const StrideLog Log = runTaps(Fight, Taps);
            INFO(std::format("feet off center {:.3f}, pelvis {:.3f} (stance {:.3f}), slip {:.3f}", Log.FeetOffCenter,
                             Log.LowestPelvis, StanceHeight, Log.TotalSlip));
            CHECK(Log.FeetOffCenter <= MaxFeetOffCenter);
            CHECK(Log.LowestPelvis >= StanceHeight - MaxPelvisSink);
            CHECK(Log.TotalSlip < MaxPlantedSlipTotal);
        }
    }
}
