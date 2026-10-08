//===- combat/move_measure.hpp - Measure a move on a stand ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares measureMove(), which runs one move headlessly and
/// reports how long its phases take, where the striking part goes, how far
/// it reaches and whether it hits a dummy; and MoveRun and prepareStand(),
/// the pieces it is made of, which the move stand of the sandbox
/// (`fighter_app --stand`) uses to repeat a move in a window.
///
/// The stand is a Battle of two fighters: the attacker (left) holds the
/// weapon, if any, in the main hand and presses the input that its moveset
/// maps to the move; the dummy (right) is a bare fighter that gets no input.
/// Both are built from data/stand.json (combat/stand_config.hpp). Without a
/// dummy it stands far away, so that nothing is hit and the strike is
/// not stopped by it.
///
/// The measurements:
///
///   - Startup, Active and Recovery: the seconds the attacker's attack
///     spends in each phase (FighterView::Phase), real time: the fighter's
///     DEX, the weapon's speed and the stamina already count;
///   - the path of a striking part: its tip every step. The tip of a part
///     is the end of its long axis that is further from the pelvis, and for
///     the forearm that holds the weapon of a move made with the weapon
///     (MoveDef::UsesWeapon) the point at the weapon's reach beyond it. A
///     clip may be played mirrored (a kick with the other foot in front, a
///     weapon in the other hand), so the paths are kept for the clip's
///     striking parts and their mirrors, and of each such pair only the
///     one whose tip travelled further is reported;
///   - the reach: the greatest forward distance (towards the opponent) from
///     the attacker's pelvis, where it stood when the move began, to a
///     tip during the active phase. The pelvis is chosen, not the front
///     foot, because it is the point the fight keeps its distances by
///     (spacing, the AI's "ai.range_m"); a clip that moves the pelvis (its
///     pelvis track, a lunge) reaches further by that travel;
///   - the pelvis travel: how far forward (and back) the pelvis went from
///     where it stood when the move began, and where it ended (a clip's
///     pelvis track; the offset stays after the move);
///   - the hit: the first StrikeLanded of the move: when, which part of the
///     dummy, the approach speed, impulse and strength of the contact,
///     damage, reaction, blocked or not.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "combat/battle.hpp"
#include "combat/commands.hpp"
#include "combat/move_input.hpp"
#include "combat/moves.hpp"
#include "combat/snapshot.hpp"
#include "combat/stand_config.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"

namespace fighter::combat {

/// What to measure.
struct MeasureRequest {
    std::string MoveId;                      ///< data/moves/<MoveId>.json.
    /// The item the attacker holds in the main hand (data/items/); empty:
    /// bare hands, and if the move is not in the unarmed set, the first
    /// item whose moveset has it.
    std::string WeaponId;
    bool WithDummy = true;                   ///< A dummy to hit; otherwise it stands far away.
    std::filesystem::path DataDir = "data";
};

/// A request resolved against the data: the battle, the input that starts the
/// move and the names the stand shows.
struct StandSetup {
    BattleConfig Config;                     ///< Left: the attacker, Right: the dummy.
    StandConfig Settings;
    PlayerCommands Input;                    ///< What the attacker presses, facing right.
    std::string InputText;                   ///< "Forward+Heavy".
    std::string MoveId;
    std::string MoveSetId;                   ///< The set that maps the input.
    std::string WeaponId;                    ///< Empty: bare hands.
    std::string WeaponName;
    std::string ClipName;                    ///< The clip the move plays at the dummy's distance.
    /// How far the weapon sticks out beyond the fist for this move, m; 0 for
    /// a move made without the weapon.
    float WeaponReachM = 0.0f;
    bool WithDummy = true;
    /// The parts whose paths are kept: the striking parts of the move's
    /// clips and their mirrored legs.
    std::vector<BodyPart> Candidates;
};

/// Resolves \p Request: reads the data, finds the weapon and the input.
/// Throws std::runtime_error that names the move or item when the move or
/// the item is unknown, the item cannot be held, or no input of the
/// weapon's moveset starts the move.
StandSetup prepareStand(const MeasureRequest& Request);

/// One point of the path of a striking part.
struct TrajectoryPoint {
    float TimeSec = 0.0f;                    ///< Since the move began.
    Vec2 Tip;                                ///< World, m.
    AttackPhase Phase = AttackPhase::None;
};

struct StrikerPath {
    BodyPart Part = BodyPart::ForearmL;
    std::vector<TrajectoryPoint> Points;
    /// The greatest forward distance of a tip in the active phase, m;
    /// 0 if there is no active phase.
    float ReachM = 0.0f;
    Vec2 ReachPoint;
    float ReachTimeSec = 0.0f;
    /// The farthest the tip got from where it began, m: a part that only
    /// rests (the other arm) travels little.
    float TravelM = 0.0f;
};

struct MoveMeasure {
    bool Started = false;                    ///< The attacker began the move.
    bool Finished = false;                   ///< The measure is complete (also when it failed).
    std::string Problem;                     ///< Why it failed; empty if it did not.
    float StartupSec = 0.0f;
    float ActiveSec = 0.0f;
    float RecoverySec = 0.0f;
    std::vector<StrikerPath> Paths;          ///< One per striking part that was kept.
    Vec2 PelvisStart;                        ///< The attacker's pelvis when the move began.
    /// The attacker's pelvis every step of the move, world, m.
    std::vector<Vec2> PelvisPath;
    /// The pelvis travel from PelvisStart along the facing, m: the farthest
    /// forward (>= 0), the farthest back (<= 0) and at the end of the move.
    float PelvisForwardM = 0.0f;
    float PelvisBackM = 0.0f;
    float PelvisEndM = 0.0f;
    bool FacingRight = true;
    /// The path with the greatest reach, or nullptr.
    const StrikerPath* findFarthestPath() const;
    float getReachM() const;
    float getTotalSec() const { return StartupSec + ActiveSec + RecoverySec; }

    bool Hit = false;                        ///< The move landed on the dummy.
    float HitTimeSec = 0.0f;
    BodyPart HitPart = BodyPart::Torso;      ///< The dummy's part that was hit.
    Vec2 HitPoint;                           ///< Where, world, m.
    float ApproachSpeed = 0.0f;              ///< Of the contact, m/s (HitEvent::ApproachSpeed).
    float Impulse = 0.0f;                    ///< Of the contact, N*s (HitEvent::Impulse).
    float Strength = 0.0f;                   ///< StrikeLanded::Strength, m/s.
    float Damage = 0.0f;
    ReactionLevel Reaction = ReactionLevel::None;
    bool Blocked = false;
};

/// "startup 0.16 s, active 0.12 s, recovery 0.25 s; reach 0.71 m (ForearmL);
/// pelvis +0.25 m, ends +0.10 m; hit Head at 0.18 s" (the pelvis only when it
/// moved).
std::string describeMeasure(const MoveMeasure& Result);

/// The forward distance of \p Point from \p Origin for a fighter facing
/// right or left: positive towards the opponent.
float getForwardDistance(Vec2 Origin, Vec2 Point, bool FacingRight);

/// The tip of \p Part as the stand measures it: the end of its long axis
/// that is further from \p Pelvis, plus \p WeaponReachM further along the
/// axis (0 for a part that holds no weapon).
Vec2 getPartTip(const PartTransform& Part, Vec2 Pelvis, float WeaponReachM);

/// One run of a move on a stand: the attacker's input and the recording.
/// Each step: getAttackerCommands(), Battle::update(), observe(). The
/// dummy gets no input.
class MoveRun {
public:
    explicit MoveRun(const StandSetup& Setup, double NewStepSec = 1.0 / 60.0);

    /// The attacker's input for the next step.
    PlayerCommands getAttackerCommands() const;
    /// Reads the battle after a step: the snapshot and the events.
    void observe(const Battle& After);

    const MoveMeasure& getMeasure() const { return Measure; }
    bool isFinished() const { return Measure.Finished; }
    /// Finished and the pause of StandConfig::RepeatPauseSec has passed:
    /// the stand starts the next run.
    bool isOver() const { return Measure.Finished && PauseLeftSec <= 0.0f; }

private:
    enum class Stage : uint8_t { Settling, Pressing, Performing, Done };

    void finish(const std::string& Problem);
    /// Fills Reach* of the paths, drops the mirrored duplicates.
    void summarize();
    void recordTips(const FighterView& Attacker);

    StandSetup Setup;
    double StepSec;
    Stage Phase = Stage::Settling;
    double ClockSec = 0.0;                   ///< Since the run began.
    double StageStartSec = 0.0;
    float PauseLeftSec = 0.0f;
    MoveMeasure Measure;
};

/// Runs the move once on a new battle until it ends and returns what was
/// measured. A move that does not start or does not end is reported in
/// MoveMeasure::Problem. Throws like prepareStand(); also if the battle's
/// data is broken.
MoveMeasure measureMove(const MeasureRequest& Request);

/// Draws the paths of \p Result (debug::Cat::Trajectory, the active phase in
/// debug::Cat::Hitbox), the reach and the hit, and writes the stand lines of
/// the debug panel. Does nothing in the release build.
void drawMeasure(const StandSetup& Setup, const MoveMeasure& Result);

} // namespace fighter::combat
