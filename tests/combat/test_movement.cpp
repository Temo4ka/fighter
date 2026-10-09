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
/// A sole this high is clearly off the floor, m.
constexpr float ClearlyLiftedM = 0.02f;
/// Walking again, a foot lifts within this much travel, m.
constexpr float ResumeStepTravelM = 0.15f;
/// Feet that moved less than this did not move, m.
constexpr float StillM = 0.001f;
/// A foot on the floor moves less than this in a step (the foot IK settles
/// a few millimetres when it plants), m.
constexpr float MaxPlantedSlipPerTick = 0.005f;
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
        run(Fight, {}, {}, TicksPerSecond);
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
    /// The debug draw showed the arc of a stepping foot (debug build).
    bool ArcDrawn = false;
};

/// Did the last step draw the arc of a stepping foot (its "step FootX"
/// label in the TargetPose category)? Always false in the release build.
bool isStepArcDrawn() {
#if FIGHTER_DEBUG
    const debug::DrawList& List = debug::getDrawList();
    for (const debug::Primitive& Prim : List.getPrimitives()) {
        if (Prim.Kind == debug::PrimitiveKind::Text && Prim.Category == debug::Cat::TargetPose &&
            List.getText(Prim).starts_with("step Foot")) {
            return true;
        }
    }
#endif
    return false;
}

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
        Trace.ArcDrawn = Trace.ArcDrawn || isStepArcDrawn();
        Before = Now;
    }
    return Trace;
}

} // namespace

TEST_CASE("Movement: a walk stopped in any phase comes back into the stance around a foot that stays",
          "[combat][movement][slow]") {
    int Tried = 0;
    bool RestedLeft = false;
    bool RestedRight = false;
    const auto getSpread = [](const FighterView& View) {
        return std::abs(getAnkleX(View, BodyPart::FootL) - getAnkleX(View, BodyPart::FootR));
    };
    const auto getOffCenter = [](const FighterView& View) {
        return (getAnkleX(View, BodyPart::FootL) + getAnkleX(View, BodyPart::FootR)) * 0.5f - getPelvisX(View);
    };
    Battle Start(makeConfig());
    run(Start, {}, {}, TicksPerSecond / 4);
    const float StanceSpread = getSpread(getLeft(Start));
    const float StanceOffCenter = std::abs(getOffCenter(getLeft(Start)));
    for (int Ticks = FirstRelease; Ticks < FirstRelease + CycleTicks; Ticks += 2) {
        Battle Fight(makeConfig());
        walkAndRelease(Fight, Ticks);
        ++Tried;
        INFO("released after " << Ticks << " ticks");
        // A foot on the floor does not slide; the first foot down after the
        // release stays where it landed (the swing foot of the step going
        // on is set down where it is), the other one steps.
        std::array<std::optional<float>, 2> FirstDown;
        FighterView Before = getLeft(Fight);
        const auto watch = [&](const FighterView& View) {
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                const bool Down = getSoleHeight(View, Foot) < LandedHeight;
                if (Down && getSoleHeight(Before, Foot) < LandedHeight) {
                    CHECK(std::abs(getAnkleX(View, Foot) - getAnkleX(Before, Foot)) < MaxPlantedSlipPerTick);
                }
                std::optional<float>& At = FirstDown[Foot == BodyPart::FootL ? 0 : 1];
                if (!At && Down) At = getAnkleX(View, Foot);
            }
            Before = View;
        };
        for (int Tick = 0; Tick < TicksPerSecond * 5 / 4; ++Tick) {
            run(Fight, {}, {}, 1);
            watch(getLeft(Fight));
        }
        // In the stance: as wide as it, centered as it, and one foot where
        // it first stood.
        const FighterView& Rest = getLeft(Fight);
        CHECK(areFeetDown(Rest));
        CHECK(Rest.State == FighterState::Idle);
        CHECK(std::abs(getSpread(Rest) - StanceSpread) < 0.015f);
        CHECK(std::abs(std::abs(getOffCenter(Rest)) - StanceOffCenter) < 0.015f);
        const bool OneStayed = std::ranges::any_of(std::array{BodyPart::FootL, BodyPart::FootR}, [&](BodyPart Foot) {
            const std::optional<float>& At = FirstDown[Foot == BodyPart::FootL ? 0 : 1];
            return At && std::abs(getAnkleX(Rest, Foot) - *At) < 0.01f;
        });
        CHECK(OneStayed);
        (getFrontFoot(Rest) == BodyPart::FootL ? RestedLeft : RestedRight) = true;
    }
    CHECK(Tried >= CycleTicks / 2);
    // Either foot may end up in front.
    CHECK(RestedLeft);
    CHECK(RestedRight);
}

TEST_CASE("Movement: walking again goes on from the phase the legs rest at", "[combat][movement][slow]") {
    for (const BodyPart Front : {BodyPart::FootL, BodyPart::FootR}) {
        INFO("front " << getBodyPartName(Front));
        Battle Fight(makeConfig());
        REQUIRE(stopWithFront(Fight, makeConfig(), Front));
        const FighterView Stopped = getLeft(Fight);
        run(Fight, {}, {}, 2 * TicksPerSecond);
        CHECK(getFrontFoot(getLeft(Fight)) == Front);
        CHECK(getFeetMove(Stopped, getLeft(Fight)) < StillM);

        // The cycle goes on from the rest phase (the step it rested in, or
        // the next one): within a short walk one foot lifts and steps while
        // the other one stays planted where it stood.
        const FighterView Rest = getLeft(Fight);
        std::optional<BodyPart> Lifted;
        for (int Tick = 0; Tick < TicksPerSecond && !Lifted; ++Tick) {
            run(Fight, {.MoveX = 1.0f}, {}, 1);
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                if (getSoleHeight(getLeft(Fight), Foot) > ClearlyLiftedM) Lifted = Foot;
            }
            if (getPelvisX(getLeft(Fight)) - getPelvisX(Rest) > ResumeStepTravelM) break;
        }
        REQUIRE(Lifted);
        const BodyPart Standing = getOtherFoot(*Lifted);
        CHECK(std::abs(getAnkleX(getLeft(Fight), Standing) - getAnkleX(Rest, Standing)) < 0.02f);
    }
}

TEST_CASE("Movement: a jab after a stop leaves the legs as they rest", "[combat][movement][slow]") {
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
        // The step is drawn: its arc and where the foot lands.
        CHECK(Trace.ArcDrawn == static_cast<bool>(FIGHTER_DEBUG));
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

TEST_CASE("Movement: from the crouch a downward strike starts at once, a low block too", "[combat][movement]") {
    // The rogue's sword set maps Down+Heavy to the low cut.
    BattleConfig Config = makeConfig();
    Config.Left = loadFighter("rogue");
    Battle Fight(Config);
    const float StandingPelvis = getPart(getLeft(Fight), BodyPart::Pelvis).Position.Y;
    run(Fight, {.Down = true}, {}, TicksPerSecond / 2);
    run(Fight, {.Down = true, .Heavy = true}, {}, 1);
    CHECK(getLeft(Fight).State == FighterState::Attacking);
    CHECK(getLeft(Fight).MoveId == "sword_low_cut");
    // Struck from the crouch: the crouch stays below the strike, the pelvis
    // lower than standing during the startup.
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
    // Down+Kick has no move of its own: it falls back to the body kick.
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
    CHECK(Stopping.starts_with("settling into the stance"));
    // The stride line tells the settle.
    const std::string Stride = getLine("P1 stride");
    CHECK(Stride.starts_with("settle"));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getLine("P1 legs").starts_with("resting in the stance"));
    CHECK(getLine("P1 upper") == "stance");
    // A jab plays on the upper body only.
    run(Fight, press(MoveButton::Jab), {}, 3);
    CHECK(getLine("P1 upper").starts_with("jab"));
    CHECK(getLine("P1 legs").starts_with("resting in the stance"));
    run(Fight, {}, {}, TicksPerSecond);
    // A kick takes the legs: the action on the panel (its steps, when the
    // feet are off its pose, and their arc: "a kick after a stop ...").
    Fight.update(press(MoveButton::BodyKick), {}, Dt);
    Fight.update({}, {}, Dt);
    CHECK(getLine("P1 legs").starts_with("action kick"));
    CHECK_FALSE(getLine("P1 stride").empty());
    run(Fight, {}, {}, TicksPerSecond);
    run(Fight, {.MoveX = 1.0f, .Down = true}, {}, 20);
    CHECK(getLine("P1 legs").starts_with("crouch walk"));
#else
    SUCCEED("no panel in the release build");
#endif
}


TEST_CASE("Movement: every sword input starts its own strike", "[combat][movement]") {
    // The keys a player presses with a sword (data/movesets/sword.json).
    struct InputCase {
        PlayerCommands Cmd;
        std::string_view Move;
    };
    const InputCase Cases[] = {
        {{.Light = true}, "sword_cut"},
        {{.Heavy = true}, "sword_slash"},
        {{.MoveX = 1.0f, .Heavy = true}, "sword_thrust"},
        {{.Down = true, .Heavy = true}, "sword_low_cut"},
        {{.Up = true, .Heavy = true}, "sword_rising"},
        {{.Special = true}, "sword_spin"},
    };
    for (const auto& Case : Cases) {
        INFO("Move " << Case.Move);
        BattleConfig Config = makeConfig();
        Config.Left = loadFighter("shieldman");
        Battle Fight(Config);
        run(Fight, {}, {}, TicksPerSecond / 2);
        run(Fight, Case.Cmd, {}, 1);
        CHECK(getLeft(Fight).State == FighterState::Attacking);
        CHECK(getLeft(Fight).MoveId == Case.Move);
        // The strike plays to its end and the fighter is free again.
        run(Fight, {}, {}, TicksPerSecond * 2);
        CHECK(getLeft(Fight).State != FighterState::Attacking);
    }
}
