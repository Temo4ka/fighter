#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// Scenarios of the movement polish: the layered walk (a walk stops on both
// feet wide apart and the legs rest there; a punch leaves them, a kick steps
// into its pose), the step back in block and walking crouched.

namespace {

/// A foot whose sole is this close to the floor stands on it, m (lifted in a
/// step it is 4-20 cm up).
constexpr float FootDownHeight = 0.015f;
/// A sole this close to the floor has landed: from then on a foot that
/// moves slides, m.
constexpr float LandedHeight = 0.005f;
/// Feet that moved less than this did not move, m.
constexpr float StillM = 0.001f;
/// A walk released after this many ticks and those up to a cycle later
/// (0.8 s of clip at 1.2 m/s: 48 ticks) stop in every phase.
constexpr int FirstRelease = 40;
constexpr int CycleTicks = 48;

const CombatTuning& getTuning() {
    static const CombatTuning Tuning = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json");
    return Tuning;
}

const rig::RigDef& getRigDef() {
    static const rig::RigDef Def = rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json");
    return Def;
}

const rig::ControlParams& getControl() { return getRigDef().Control; }

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

/// The ankle hinge of \p Foot along the arena, m: the shin's end, from the
/// rig file (a shin body is centered on its bounds).
float getAnkleX(const FighterView& View, BodyPart Foot) {
    const BodyPart Shin = Foot == BodyPart::FootL ? BodyPart::ShinL : BodyPart::ShinR;
    const rig::RigDef& Def = getRigDef();
    const rig::PartDef& Shape = Def.getPart(Shin);
    const float Center = (std::min(Shape.Begin.Y, Shape.End.Y) + std::max(Shape.Begin.Y, Shape.End.Y)) * 0.5f;
    float AnchorY = 0.0f;
    for (const auto& Joint : Def.Joints) {
        if (Joint.Child == Foot) AnchorY = Joint.Anchor.Y;
    }
    const PartTransform& Part = getPart(View, Shin);
    return Part.Position.X - std::sin(Part.Angle) * (AnchorY - Center);
}

/// The foot in front (towards where the fighter faces).
BodyPart getFrontFoot(const FighterView& View) {
    const float Ahead = getFootX(View, BodyPart::FootR) - getFootX(View, BodyPart::FootL);
    return (View.FacingRight ? Ahead > 0.0f : Ahead < 0.0f) ? BodyPart::FootR : BodyPart::FootL;
}

BodyPart getOtherFoot(BodyPart Foot) { return Foot == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL; }

/// How far each foot moved between two views, the larger, m.
float getFeetMove(const FighterView& From, const FighterView& To) {
    return std::max(std::abs(getFootX(To, BodyPart::FootL) - getFootX(From, BodyPart::FootL)),
                    std::abs(getFootX(To, BodyPart::FootR) - getFootX(From, BodyPart::FootR)));
}

/// P1 walks forward for \p Ticks while P2 backs away, then neither presses
/// anything.
void walkAndRelease(Battle& Fight, int Ticks) { run(Fight, {.MoveX = 1.0f}, {.MoveX = 1.0f}, Ticks); }

/// Walks P1, releases and lets the legs rest with \p Front in front; false
/// if no release of a cycle ends so.
bool stopWithFront(Battle& Fight, const BattleConfig& Config, BodyPart Front) {
    for (int Ticks = FirstRelease; Ticks < FirstRelease + CycleTicks; ++Ticks) {
        Fight = Battle(Config);
        walkAndRelease(Fight, Ticks);
        run(Fight, {}, {}, TicksPerSecond / 2);
        if (getFrontFoot(getLeft(Fight)) == Front) return true;
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
    std::optional<BodyPart> Striker; ///< The part that landed.
    float Reach = 0.0f;              ///< Farthest a foot went in front of the pelvis, m.
    float PelvisMove = 0.0f;         ///< Largest pelvis travel from the press during the startup, m.
    /// Largest horizontal move in one step of a foot on the floor during the
    /// startup (a planted foot sliding), m.
    float GroundedSlide = 0.0f;
    /// A foot moved along the floor in the startup and was lifted while it
    /// did: the lowest sole of a moving foot, m (none: no foot moved).
    std::optional<float> LowestMovingSole;
};

/// P1 presses \p Button once and holds it until the attack starts.
StrikeTrace throwStrike(Battle& Fight, MoveButton Button) {
    StrikeTrace Trace;
    const float PelvisX = getPelvisX(getLeft(Fight));
    FighterView Before = getLeft(Fight);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        const bool Started = getLeft(Fight).State == FighterState::Attacking;
        Fight.update(Started ? PlayerCommands{} : press(Button), {}, Dt);
        const FighterView& Now = getLeft(Fight);
        if (!Trace.ActiveTick && Now.Phase == AttackPhase::Active) Trace.ActiveTick = Tick;
        if (!Trace.ActiveTick) {
            Trace.PelvisMove = std::max(Trace.PelvisMove, std::abs(getPelvisX(Now) - PelvisX));
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                const float Move = std::abs(getAnkleX(Now, Foot) - getAnkleX(Before, Foot));
                if (Move < StillM) continue;
                const float Sole = std::max(getSoleHeight(Now, Foot), getSoleHeight(Before, Foot));
                Trace.LowestMovingSole = std::min(Trace.LowestMovingSole.value_or(Sole), Sole);
                if (Sole < LandedHeight) Trace.GroundedSlide = std::max(Trace.GroundedSlide, Move);
            }
        }
        for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
            const float Facing = Now.FacingRight ? 1.0f : -1.0f;
            Trace.Reach = std::max(Trace.Reach, (getFootX(Now, Foot) - getPelvisX(Now)) * Facing);
        }
        for (const auto& Hit : getHits(Fight)) Trace.Striker = Hit.Attacker.Part;
        Before = Now;
    }
    return Trace;
}

} // namespace

TEST_CASE("Movement: a walk stopped in any phase rests on both feet wide apart, no foot slides",
          "[combat][movement]") {
    int Tried = 0;
    bool RestedLeft = false;
    bool RestedRight = false;
    for (int Ticks = FirstRelease; Ticks < FirstRelease + CycleTicks; Ticks += 2) {
        Battle Fight(makeConfig());
        walkAndRelease(Fight, Ticks);
        ++Tried;
        INFO("released after " << Ticks << " ticks");
        // A foot down when the key is released is planted: from then on its
        // ankle does not move (the foot may still roll flat about it). The
        // other one lands within 0.25 s and stays too.
        std::array<std::optional<float>, 2> Planted;
        const auto watch = [&](const FighterView& View) {
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                std::optional<float>& At = Planted[Foot == BodyPart::FootL ? 0 : 1];
                if (!At && getSoleHeight(View, Foot) < LandedHeight) At = getAnkleX(View, Foot);
                if (At) CHECK(std::abs(getAnkleX(View, Foot) - *At) < StillM);
            }
        };
        watch(getLeft(Fight));
        int Landed = 0;
        for (; Landed < TicksPerSecond / 4 && !areFeetDown(getLeft(Fight)); ++Landed) {
            run(Fight, {}, {}, 1);
            watch(getLeft(Fight));
        }
        CHECK(areFeetDown(getLeft(Fight)));
        for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
            run(Fight, {}, {}, 1);
            watch(getLeft(Fight));
        }
        // No extra step, no blend to the stance: the feet stay, wide apart.
        const FighterView& Rest = getLeft(Fight);
        CHECK(areFeetDown(Rest));
        CHECK(Rest.State == FighterState::Idle);
        CHECK(std::abs(getAnkleX(Rest, BodyPart::FootL) - getAnkleX(Rest, BodyPart::FootR)) >=
              getTuning().RestMinFootSpread);
        (getFrontFoot(Rest) == BodyPart::FootL ? RestedLeft : RestedRight) = true;
    }
    CHECK(Tried >= CycleTicks / 2);
    // Both wide double supports of the cycle are rest poses.
    CHECK(RestedLeft);
    CHECK(RestedRight);
}

TEST_CASE("Movement: walking again goes on from the phase the legs rest at", "[combat][movement]") {
    for (const BodyPart Front : {BodyPart::FootL, BodyPart::FootR}) {
        INFO("front " << getBodyPartName(Front));
        Battle Fight(makeConfig());
        REQUIRE(stopWithFront(Fight, makeConfig(), Front));
        const FighterView Stopped = getLeft(Fight);
        run(Fight, {}, {}, 2 * TicksPerSecond);
        CHECK(getFrontFoot(getLeft(Fight)) == Front);
        CHECK(getFeetMove(Stopped, getLeft(Fight)) < StillM);

        // The cycle goes on from the rest phase: the rear foot steps first,
        // the front one stays planted at first.
        const float FrontX = getFootX(getLeft(Fight), Front);
        run(Fight, {.MoveX = 1.0f}, {}, 6);
        CHECK(std::abs(getFootX(getLeft(Fight), Front) - FrontX) < 0.02f);
        CHECK_FALSE(isFootDown(getLeft(Fight), getOtherFoot(Front)));
    }
}

TEST_CASE("Movement: a jab after a stop leaves the legs as they rest", "[combat][movement]") {
    for (const BodyPart Front : {BodyPart::FootL, BodyPart::FootR}) {
        INFO("front " << getBodyPartName(Front));
        Battle Fight(makeConfig());
        REQUIRE(stopWithFront(Fight, makeConfig(), Front));
        const FighterView Rest = getLeft(Fight);
        Fight.update(press(MoveButton::Jab), {}, Dt);
        bool Jabbed = false;
        for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
            Fight.update({}, {}, Dt);
            const FighterView& Now = getLeft(Fight);
            Jabbed = Jabbed || Now.MoveId == "jab";
            for (const BodyPart Part : {BodyPart::Pelvis, BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL,
                                        BodyPart::ThighR, BodyPart::ShinR, BodyPart::FootR}) {
                INFO(getBodyPartName(Part) << " at tick " << Tick);
                CHECK(std::abs(getPart(Now, Part).Position.X - getPart(Rest, Part).Position.X) < StillM);
                CHECK(std::abs(getPart(Now, Part).Angle - getPart(Rest, Part).Angle) < 0.002f);
            }
        }
        CHECK(Jabbed);
    }
}

TEST_CASE("Movement: a kick after a stop steps into its pose and strikes on time", "[combat][movement]") {
    // The reference: the kick from the stance at the start.
    Battle Standing(makeConfig());
    run(Standing, {}, {}, TicksPerSecond / 2);
    const StrikeTrace Reference = throwStrike(Standing, MoveButton::BodyKick);
    REQUIRE(Reference.ActiveTick);

    for (const BodyPart Front : {BodyPart::FootL, BodyPart::FootR}) {
        INFO("front " << getBodyPartName(Front));
        Battle Fight(makeConfig());
        REQUIRE(stopWithFront(Fight, makeConfig(), Front));
        const StrikeTrace Trace = throwStrike(Fight, MoveButton::BodyKick);
        // The step is woven into the startup: the kick is active on the
        // same tick as from the stance, and reaches as far.
        REQUIRE(Trace.ActiveTick);
        CHECK(*Trace.ActiveTick == *Reference.ActiveTick);
        CHECK(Trace.Reach == Approx(Reference.Reach).margin(0.02f));
        // A real step: the moving foot is lifted while it moves, the planted
        // one does not slide, and the pelvis stays where it is.
        REQUIRE(Trace.LowestMovingSole);
        CHECK(*Trace.LowestMovingSole > 0.0f);
        CHECK(Trace.GroundedSlide < StillM);
        CHECK(Trace.PelvisMove < StillM);
    }
}

TEST_CASE("Movement: with the right foot in front the kick plays mirrored and lands with it",
          "[combat][movement]") {
    Battle Fight(makeConfig());
    REQUIRE(stopWithFront(Fight, makeConfig(), BodyPart::FootR));
    bringVictim(Fight, KickRange);
    REQUIRE(getFrontFoot(getLeft(Fight)) == BodyPart::FootR);
    const StrikeTrace Trace = throwStrike(Fight, MoveButton::BodyKick);
    REQUIRE(Trace.Striker);
    // The right leg kicks: its parts are the strikers that hit.
    CHECK((Trace.Striker == BodyPart::FootR || Trace.Striker == BodyPart::ShinR));
    // And the legs rest in the stance with the right foot in front after it.
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getFrontFoot(getLeft(Fight)) == BodyPart::FootR);
}

TEST_CASE("Movement: stanceAfterStop authored kicks with the authored leg after a lifted step",
          "[combat][movement]") {
    ScratchData Data("movement_authored");
    Data.replace("combat.json", "\"stanceAfterStop\": \"mirror\"", "\"stanceAfterStop\": \"authored\"");
    Battle Standing(Data.makeConfig());
    run(Standing, {}, {}, TicksPerSecond / 2);
    const StrikeTrace Reference = throwStrike(Standing, MoveButton::BodyKick);
    REQUIRE(Reference.ActiveTick);

    Battle Fight(Data.makeConfig());
    REQUIRE(stopWithFront(Fight, Data.makeConfig(), BodyPart::FootR));
    const float LeftStart = getFootX(getLeft(Fight), BodyPart::FootL);
    const StrikeTrace Trace = throwStrike(Fight, MoveButton::BodyKick);
    REQUIRE(Trace.ActiveTick);
    CHECK(*Trace.ActiveTick == *Reference.ActiveTick);
    CHECK(Trace.Reach == Approx(Reference.Reach).margin(0.02f));
    REQUIRE(Trace.LowestMovingSole);
    CHECK(*Trace.LowestMovingSole > 0.0f);
    CHECK(Trace.GroundedSlide < StillM);
    // The left (rear) leg kicked: after the kick the left foot is in front.
    CHECK(getFootX(getLeft(Fight), BodyPart::FootL) > LeftStart + 0.1f);
    CHECK(getFrontFoot(getLeft(Fight)) == BodyPart::FootL);
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

TEST_CASE("Movement: the debug panel shows the leg layer and the upper body", "[combat][movement]") {
#if FIGHTER_DEBUG
    const auto getLine = [](const std::string& Wanted) {
        for (const auto& [Key, Value] : debug::getDrawList().getPanel()) {
            if (Key == Wanted) return Value;
        }
        return std::string();
    };
    Battle Fight(makeConfig());
    run(Fight, {}, {}, 1);
    CHECK(getLine("P1 legs").starts_with("resting in the stance"));
    run(Fight, {.MoveX = 1.0f}, {}, 20);
    CHECK(getLine("P1 legs").starts_with("walking"));
    run(Fight, {}, {}, 1);
    const std::string Stopping = getLine("P1 legs");
    CHECK((Stopping.starts_with("stopping") || Stopping.starts_with("resting at phase")));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getLine("P1 legs").starts_with("resting at phase"));
    CHECK(getLine("P1 upper") == "stance");
    // A jab plays on the upper body only.
    run(Fight, press(MoveButton::Jab), {}, 3);
    CHECK(getLine("P1 upper").starts_with("jab"));
    CHECK(getLine("P1 legs").starts_with("resting at phase"));
    run(Fight, {}, {}, TicksPerSecond);
    // A kick steps into its pose: the step on the panel, its arc drawn.
    // (The steps start the tick after the press.)
    Fight.update(press(MoveButton::BodyKick), {}, Dt);
    Fight.update({}, {}, Dt);
    CHECK(getLine("P1 legs").starts_with("action kick"));
    CHECK(getLine("P1 legs").find("step Foot") != std::string::npos);
    bool Drawn = false;
    const debug::DrawList& List = debug::getDrawList();
    for (const debug::Primitive& Prim : List.getPrimitives()) {
        if (Prim.Kind == debug::PrimitiveKind::Text && Prim.Category == debug::Cat::TargetPose &&
            List.getText(Prim).starts_with("step Foot")) {
            Drawn = true;
        }
    }
    CHECK(Drawn);
    run(Fight, {}, {}, TicksPerSecond);
    run(Fight, {.MoveX = 1.0f, .Down = true}, {}, 20);
    CHECK(getLine("P1 legs").starts_with("crouch walk"));
#else
    SUCCEED("no panel in the release build");
#endif
}

