#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "combat/battle.hpp"
#include "combat/moves.hpp"
#include "combat/tuning.hpp"
#include "debug/draw.hpp"
#include "rig/rig_def.hpp"
#include "scenario.hpp"

#if FIGHTER_DEBUG
#include "debug/draw_list.hpp"
#endif

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

// Scenarios of the movement polish after the first look at the clips:
// stopping a walk on both feet (the normal and the switched stance), the
// step back in block and walking crouched.

namespace {

/// A foot whose sole is this close to the floor stands on it, m (lifted in a
/// step it is 4-20 cm up).
constexpr float FootDownHeight = 0.015f;
/// Feet that moved less than this did not move, m.
constexpr float StillM = 0.001f;

const CombatTuning& getTuning() {
    static const CombatTuning Tuning = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json");
    return Tuning;
}

const rig::ControlParams& getControl() {
    static const rig::ControlParams Control =
        rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json").Control;
    return Control;
}

PlayerCommands press(MoveButton Button) {
    PlayerCommands Cmd;
    Cmd.Jab = Button == MoveButton::Jab;
    Cmd.HeavyPunch = Button == MoveButton::HeavyPunch;
    Cmd.BodyKick = Button == MoveButton::BodyKick;
    Cmd.LowKick = Button == MoveButton::LowKick;
    return Cmd;
}

float getFootX(const FighterView& View, BodyPart Foot) { return getPart(View, Foot).Position.X; }

/// The lowest point of a foot above the floor, from its turned bounding box.
float getSoleHeight(const FighterView& View, BodyPart Foot) {
    const PartTransform& Part = getPart(View, Foot);
    const float Drop = std::abs(std::sin(Part.Angle)) * Part.Size.X * 0.5f +
                       std::abs(std::cos(Part.Angle)) * Part.Size.Y * 0.5f;
    return Part.Position.Y - Drop;
}

bool isFootDown(const FighterView& View, BodyPart Foot) { return getSoleHeight(View, Foot) < FootDownHeight; }

bool areFeetDown(const FighterView& View) {
    return isFootDown(View, BodyPart::FootL) && isFootDown(View, BodyPart::FootR);
}

/// The right foot in front: the switched stance.
bool isSwitched(const FighterView& View) {
    const float Ahead = getFootX(View, BodyPart::FootR) - getFootX(View, BodyPart::FootL);
    return View.FacingRight ? Ahead > 0.0f : Ahead < 0.0f;
}

/// How far each foot moved between two views, the larger, m.
float getFeetMove(const FighterView& From, const FighterView& To) {
    return std::max(std::abs(getFootX(To, BodyPart::FootL) - getFootX(From, BodyPart::FootL)),
                    std::abs(getFootX(To, BodyPart::FootR) - getFootX(From, BodyPart::FootR)));
}

/// P1 walks forward for \p Ticks while P2 backs away, then neither presses
/// anything.
void walkAndRelease(Battle& Fight, int Ticks) { run(Fight, {.MoveX = 1.0f}, {.MoveX = 1.0f}, Ticks); }

/// Walks P1 until it stops in the switched stance; false if no release in
/// the tried range ends there.
bool stopSwitched(Battle& Fight, const BattleConfig& Config) {
    for (int Ticks = 30; Ticks < 70; ++Ticks) {
        Fight = Battle(Config);
        walkAndRelease(Fight, Ticks);
        run(Fight, {}, {}, TicksPerSecond / 2);
        if (isSwitched(getLeft(Fight))) return true;
    }
    return false;
}

/// P2 walks up to \p Range of P1 (floor points) and stops; then both rest.
void bringVictim(Battle& Fight, float Range) {
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        const bool Far = getRight(Fight).Position.X - getLeft(Fight).Position.X > Range;
        Fight.update({}, {.MoveX = Far ? -1.0f : 0.0f}, Dt);
        if (!Far) break;
    }
    run(Fight, {}, {}, TicksPerSecond / 2);
}

struct StrikeTrace {
    std::optional<int> ActiveTick;   ///< Ticks from the press to the active phase.
    bool Landed = false;
    bool NormalAtActive = false;     ///< The left foot in front when the strike lands.
};

/// P1 presses \p Button once and holds it until the attack starts.
StrikeTrace throwStrike(Battle& Fight, MoveButton Button) {
    StrikeTrace Trace;
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        const bool Started = getLeft(Fight).State == FighterState::Attacking;
        Fight.update(Started ? PlayerCommands{} : press(Button), {}, Dt);
        if (!Trace.ActiveTick && getLeft(Fight).Phase == AttackPhase::Active) {
            Trace.ActiveTick = Tick;
            Trace.NormalAtActive = !isSwitched(getLeft(Fight));
        }
        Trace.Landed = Trace.Landed || !getHits(Fight).empty();
    }
    return Trace;
}

float getMinStartup(MoveButton Button) {
    const std::vector<MoveDef> Moves = loadMoveSet(std::filesystem::path(FIGHTER_DATA_DIR) / "moves");
    const MoveDef* Found = findMove(Moves, Button, "");
    REQUIRE(Found);
    return Found->MinStartupSec;
}

} // namespace

TEST_CASE("Movement: released mid-step, the fighter stands on both feet at once", "[combat][movement]") {
    int Tried = 0;
    for (int Ticks = 40; Ticks < 70; Ticks += 2) {
        Battle Fight(makeConfig());
        walkAndRelease(Fight, Ticks);
        if (areFeetDown(getLeft(Fight))) continue;   // only releases with a foot in the air
        ++Tried;
        INFO("released after " << Ticks << " ticks");

        // The foot in the air lands within 0.15 s.
        std::optional<FighterView> Landed;
        for (int Tick = 0; Tick < 9 && !Landed; ++Tick) {
            run(Fight, {}, {}, 1);
            if (areFeetDown(getLeft(Fight))) Landed = getLeft(Fight);
        }
        REQUIRE(Landed);
        // Then no extra step: the feet only settle into the stance (a few
        // cm) and stand still from 0.4 s on, also later.
        run(Fight, {}, {}, TicksPerSecond * 2 / 5);
        const FighterView Settled = getLeft(Fight);
        CHECK(getFeetMove(*Landed, Settled) < 0.1f);
        CHECK(areFeetDown(Settled));
        run(Fight, {}, {}, TicksPerSecond);
        CHECK(getFeetMove(Settled, getLeft(Fight)) < StillM);
        CHECK(getLeft(Fight).State == FighterState::Idle);

        // It stands in one of the two stances: left foot forward or right.
        const float Pelvis = getPelvisX(Settled);
        const float Front = std::max(getFootX(Settled, BodyPart::FootL), getFootX(Settled, BodyPart::FootR)) - Pelvis;
        const float Back = std::min(getFootX(Settled, BodyPart::FootL), getFootX(Settled, BodyPart::FootR)) - Pelvis;
        Battle Fresh(makeConfig());
        const FighterView& Stance = getLeft(Fresh);
        CHECK(Front == Approx(getFootX(Stance, BodyPart::FootL) - getPelvisX(Stance)).margin(0.02f));
        CHECK(Back == Approx(getFootX(Stance, BodyPart::FootR) - getPelvisX(Stance)).margin(0.02f));
    }
    CHECK(Tried >= 5);
}

TEST_CASE("Movement: with stopSlidesFeet off the planted foot stays about where it stood", "[combat][movement]") {
    ScratchData Data("movement_no_slide");
    Data.replace("combat.json", "\"stopSlidesFeet\": true", "\"stopSlidesFeet\": false");
    int Tried = 0;
    for (int Ticks = 40; Ticks < 70; Ticks += 3) {
        Battle Fight(Data.makeConfig());
        walkAndRelease(Fight, Ticks);
        const FighterView Released = getLeft(Fight);
        const bool LeftDown = isFootDown(Released, BodyPart::FootL);
        if (LeftDown == isFootDown(Released, BodyPart::FootR)) continue;   // one foot in the air
        ++Tried;
        INFO("released after " << Ticks << " ticks");
        const BodyPart Planted = LeftDown ? BodyPart::FootL : BodyPart::FootR;
        run(Fight, {}, {}, TicksPerSecond);
        CHECK(areFeetDown(getLeft(Fight)));
        // It is dragged only when the clip pulls it further than the rig's
        // footLockSlip.
        CHECK(std::abs(getFootX(getLeft(Fight), Planted) - getFootX(Released, Planted)) < getControl().FootLockSlip);
    }
    CHECK(Tried >= 3);
}

TEST_CASE("Movement: the switched stance holds and walking goes on from it", "[combat][movement]") {
    Battle Fight(makeConfig());
    REQUIRE(stopSwitched(Fight, makeConfig()));
    const FighterView Stopped = getLeft(Fight);
    run(Fight, {}, {}, 2 * TicksPerSecond);
    CHECK(isSwitched(getLeft(Fight)));
    CHECK(getFeetMove(Stopped, getLeft(Fight)) < StillM);

    // Walking again starts from the switched phase: the left (rear) foot
    // steps first, the right one stays planted at first.
    const float RightX = getFootX(getLeft(Fight), BodyPart::FootR);
    run(Fight, {.MoveX = 1.0f}, {}, 6);
    CHECK(std::abs(getFootX(getLeft(Fight), BodyPart::FootR) - RightX) < 0.02f);
    CHECK_FALSE(isFootDown(getLeft(Fight), BodyPart::FootL));
}

TEST_CASE("Movement: from the switched stance a jab and a kick step back and land", "[combat][movement]") {
    for (const auto& [Button, Range] : {std::pair(MoveButton::Jab, JabRange), std::pair(MoveButton::BodyKick, KickRange)}) {
        INFO(getMoveButtonName(Button));
        Battle Fight(makeConfig());
        REQUIRE(stopSwitched(Fight, makeConfig()));
        bringVictim(Fight, Range);
        REQUIRE(isSwitched(getLeft(Fight)));
        const StrikeTrace Trace = throwStrike(Fight, Button);
        REQUIRE(Trace.ActiveTick);
        // The step does not make the strike come sooner than its floor.
        CHECK(static_cast<float>(*Trace.ActiveTick + 1) / TicksPerSecond >= getMinStartup(Button) - 1e-3f);
        CHECK(Trace.NormalAtActive);
        CHECK(Trace.Landed);
        CHECK_FALSE(isSwitched(getLeft(Fight)));
    }
}

TEST_CASE("Movement: blocking, forward does not move and backward steps slowly", "[combat][movement]") {
    Battle Fight(makeConfig());
    const float StartX = getPelvisX(getLeft(Fight));
    run(Fight, {.MoveX = 1.0f, .Block = true}, {}, TicksPerSecond);
    CHECK(getLeft(Fight).State == FighterState::Blocking);
    CHECK(std::abs(getPelvisX(getLeft(Fight)) - StartX) < StillM);

    run(Fight, {.MoveX = -1.0f, .Block = true}, {}, TicksPerSecond / 2);   // accelerate
    const FighterView From = getLeft(Fight);
    run(Fight, {.MoveX = -1.0f, .Block = true}, {}, TicksPerSecond);
    CHECK(getLeft(Fight).State == FighterState::Blocking);
    CHECK(getLeft(Fight).Block == BlockZone::Mid);
    const float Speed = getPelvisX(getLeft(Fight)) - getPelvisX(From);
    CHECK(Speed == Approx(-getControl().WalkSpeed * getTuning().BlockBackSpeedScale).epsilon(0.02));
    // The legs step (the walk cycle backwards), the feet do not slide along.
    CHECK(getFeetMove(From, getLeft(Fight)) > 0.1f);
}

TEST_CASE("Movement: crouched, the fighter walks slowly with the pelvis low", "[combat][movement]") {
    Battle Fight(makeConfig());
    const float StandingPelvis = getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y;
    run(Fight, {.Down = true}, {}, TicksPerSecond / 2);
    const float CrouchPelvis = getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y;
    REQUIRE(CrouchPelvis < StandingPelvis - 0.15f);

    for (const float MoveX : {1.0f, -1.0f}) {
        INFO("MoveX " << MoveX);
        run(Fight, {.MoveX = MoveX, .Down = true}, {}, TicksPerSecond / 2);   // accelerate
        const FighterView From = getLeft(Fight);
        float HighestPelvis = 0.0f;
        for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
            run(Fight, {.MoveX = MoveX, .Down = true}, {}, 1);
            CHECK(getLeft(Fight).State == FighterState::Crouching);
            HighestPelvis = std::max(HighestPelvis, getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y);
        }
        const float Speed = getPelvisX(getLeft(Fight)) - getPelvisX(From);
        const float Scale = MoveX > 0.0f ? 1.0f : getControl().BackwardSpeedScale;
        CHECK(Speed == Approx(MoveX * getControl().WalkSpeed * Scale * getTuning().CrouchWalkSpeedScale).epsilon(0.02));
        CHECK(HighestPelvis < StandingPelvis - 0.15f);
        CHECK(getFeetMove(From, getLeft(Fight)) > 0.2f);
    }
    // Stopped, it stays crouched on both feet.
    run(Fight, {.Down = true}, {}, TicksPerSecond);
    CHECK(getLeft(Fight).State == FighterState::Crouching);
    CHECK(areFeetDown(getLeft(Fight)));
}

TEST_CASE("Movement: from the crouch a low kick starts at once, a low block too", "[combat][movement]") {
    Battle Fight(makeConfig());
    const float StandingPelvis = getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y;
    run(Fight, {.Down = true}, {}, TicksPerSecond / 2);
    run(Fight, {.Down = true, .LowKick = true}, {}, 1);
    CHECK(getLeft(Fight).State == FighterState::Attacking);
    CHECK(getLeft(Fight).MoveId == "low_kick");
    // Kicked from the crouch: the right leg stays bent, the pelvis lower than
    // standing during the startup (the kicking leg of low_kick lifts it some).
    run(Fight, {.Down = true}, {}, 6);
    CHECK(getLeft(Fight).Phase == AttackPhase::Startup);
    CHECK(getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y < StandingPelvis - 0.05f);

    run(Fight, {.Down = true}, {}, TicksPerSecond);
    CHECK(getLeft(Fight).State == FighterState::Crouching);
    run(Fight, {.Down = true, .Block = true}, {}, 2);
    CHECK(getLeft(Fight).State == FighterState::Blocking);
    CHECK(getLeft(Fight).Block == BlockZone::Low);
}

TEST_CASE("Movement: from the crouch other strikes stand up first", "[combat][movement]") {
    for (const MoveButton Button : {MoveButton::Jab, MoveButton::HeavyPunch, MoveButton::BodyKick}) {
        INFO(getMoveButtonName(Button));
        Battle Fight(makeConfig());
        run(Fight, {.Down = true}, {}, TicksPerSecond / 2);
        const float CrouchHead = getHeadHeight(getLeft(Fight));
        // Down stays held: the strike still comes, after standing up.
        PlayerCommands Cmd = press(Button);
        Cmd.Down = true;
        run(Fight, Cmd, {}, 1);
        CHECK(getLeft(Fight).State == FighterState::Idle);
        int Waited = 1;
        while (getLeft(Fight).State != FighterState::Attacking && Waited < TicksPerSecond) {
            run(Fight, Cmd, {}, 1);
            ++Waited;
        }
        REQUIRE(getLeft(Fight).State == FighterState::Attacking);
        CHECK(static_cast<float>(Waited) / TicksPerSecond >= getTuning().CrouchStandUpSec - 1e-3f);
        CHECK(getHeadHeight(getLeft(Fight)) > CrouchHead + 0.1f);
        // After the strike, down held: crouched again.
        run(Fight, {.Down = true}, {}, 2 * TicksPerSecond);
        CHECK(getLeft(Fight).State == FighterState::Crouching);
    }
}

TEST_CASE("Movement: the debug panel shows the legs", "[combat][movement]") {
#if FIGHTER_DEBUG
    const auto getLine = [] {
        for (const auto& [Key, Value] : debug::getDrawList().getPanel()) {
            if (Key == "P1 legs") return Value;
        }
        return std::string();
    };
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = 1.0f}, {}, 20);
    CHECK(getLine().starts_with("walk"));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getLine().starts_with("stance"));
    run(Fight, {.MoveX = 1.0f, .Down = true}, {}, 20);
    CHECK(getLine().starts_with("crouch walk"));
#else
    SUCCEED("no panel in the release build");
#endif
}
