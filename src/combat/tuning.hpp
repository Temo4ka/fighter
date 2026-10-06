//===- combat/tuning.hpp - Battle tuning loaded from JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CombatTuning, the battle parameters that belong to
/// neither fighter's body: where the fighters start, how close they may get,
/// which contacts count as hits, stamina, chains and blocking. They are read
/// from data/combat.json when a Battle is created, so a restart (F5 in the
/// sandbox) picks up edited values.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace fighter::combat {

/// What sets the pose of a fighter, for the blend times of the changes
/// between them (BlendTable).
enum class PoseKind : uint8_t {
    Stance,      ///< Standing still: the stance, or the legs where a walk stopped.
    Walk,        ///< The walk cycle.
    Crouch,      ///< Crouched, holding still.
    CrouchWalk,  ///< The crouch walk cycle.
    Block,       ///< A block clip.
    Strike,      ///< An attack clip.
    Reaction,    ///< A hit reaction clip (flinch, stagger, knockback).
    Count
};

/// "stance", "walk", "crouch", "crouchWalk", "block", "strike", "reaction".
std::string_view getPoseKindName(PoseKind Kind);

/// How long the pose takes to cross over when what sets it changes
/// (stance <-> walk <-> crouch <-> block <-> strikes <-> reactions):
/// a default and rules for pairs of kinds (data/combat.json, "blends"). A
/// clip's own blendIn / blendOut (data/poses) overrides the table for the
/// changes into and out of that clip.
struct BlendTable {
    /// One rule: From -> To in Sec; an empty kind matches any.
    struct Rule {
        std::optional<PoseKind> From;
        std::optional<PoseKind> To;
        float Sec = 0.0f;
    };
    float DefaultSec = 0.1f;
    std::vector<Rule> Rules;
    /// A blend into a strike lasts at most this share of its startup (at
    /// its rate), so the strike shows its own pose before its active phase
    /// and its timing does not change.
    float StrikeStartupShare = 0.5f;

    /// The blend time of \p From -> \p To: the rule with both kinds, else
    /// the one with \p To and any from, else the one with \p From and any
    /// to, else the default, s.
    float getSec(PoseKind From, PoseKind To) const;
};

/// Which way round a leg action plays when the legs rest with the other
/// foot in front than the clip was authored for (a walk stopped mid-stride).
enum class StanceAfterStop : uint8_t {
    Mirror,    ///< The action's legs are swapped (anim::mirrorClipLegs): no step to switch feet.
    Authored,  ///< As authored: the legs step into the authored stance first.
};

/// "mirror", "authored".
std::string_view getStanceAfterStopName(StanceAfterStop Choice);

/// How the legs move into the pose of an action that needs them (a kick, a
/// crouch, the low block) from where they rest: real steps, see
/// combat/leg_step.hpp.
struct LegStepTuning {
    /// A foot this far or less from where the action puts it does not
    /// step (the rig's planted foot takes up the difference), m.
    float MinDistance = 0.03f;
    /// How high a stepping foot is lifted in the middle of its step, m.
    float LiftHeight = 0.06f;
    /// The steps of a strike take this share of its startup (0..1]; the
    /// startup itself does not change.
    float StartupShare = 0.8f;
    /// The steps into an action without a startup (crouch, low block), s.
    float Sec = 0.15f;
    StanceAfterStop Stance = StanceAfterStop::Mirror;
};

struct CombatTuning {
    /// Distance between the fighters' body origins at the start, m.
    float SpawnDistance = 2.4f;
    /// \name Physics steps
    /// Each simulation step is PhysicsSteps Box2D steps of PhysicsSubSteps
    /// solver substeps each. More Box2D steps: a fast limb moves less per
    /// step, so it sinks less deep into what it hits before the solver
    /// pushes it out (nothing passes through the opponent); the cost grows
    /// with PhysicsSteps * PhysicsSubSteps.
    /// @{
    int PhysicsSteps = 8;
    int PhysicsSubSteps = 4;
    /// Stiffness of the contacts between body parts, Hz: an arm pressed into
    /// the opponent by its motors sinks in less the stiffer they are. Box2D
    /// caps it at 1/8 of the substep rate (60 Hz * PhysicsSteps *
    /// PhysicsSubSteps / 8).
    float ContactHertz = 240.0f;
    /// Friction between body parts of different fighters: a limb pressed
    /// onto the opponent (a foot on a lying head) drags it along less, arms
    /// slide off each other more easily the lower it is.
    float FighterFriction = 0.6f;
    /// @}
    /// \name Allowed overlap of the fighters
    /// How deep a part of one fighter may overlap a part of the other one
    /// (Battle::findWorstOverlap, the "overlap" panel line, the invariant
    /// tests), m. Two arms pressed into each other by their motors sink in a
    /// little more than anything else.
    /// @{
    float ArmOverlapTolerance = 0.02f;
    float OverlapTolerance = 0.012f;
    /// @}
    /// Closing speed above which a contact between body parts of different
    /// fighters is reported by physics, m/s. Combat then keeps only the
    /// contacts of a striking limb in the active phase of an attack.
    float HitSpeedThreshold = 0.6f;
    /// Half the width of a fighter's pushbox, m: the pelvises stay at least
    /// twice this apart and this far from the arena walls.
    float BodyHalfWidth = 0.25f;
    /// A standing fighter steps off a body lying under it at most this
    /// fast, m/s.
    float SeparationSpeed = 4.0f;
    /// The largest correction of a step the spacing looks for (overlapping
    /// pelvises or bodies; rig/spacing.hpp), as a speed, m/s.
    float PosedSeparationSpeed = 12.0f;
    /// Beyond taking back their approach (which only slows a walk down), the
    /// spacing pushes the fighters apart at most this fast, m/s...
    float PushMaxSpeed = 1.5f;
    /// ...and that push speed changes at most this fast, m/s^2.
    float PushAcceleration = 20.0f;
    /// Bodies the eased push would leave deeper than this in each other are
    /// pushed apart at once (a hard push), m. Below overlapTolerance.
    float PushSoftOverlap = 0.008f;
    /// With no stamina left, walking and strikes are this much slower (O.13).
    float ExhaustedSpeedScale = 0.7f;
    /// An exhausted fighter is slow until its stamina is back to this share
    /// of the maximum.
    float ExhaustedRecoverFraction = 0.3f;
    /// A strike that hit may be cancelled into the next one of its chain
    /// (MoveDef::ChainTo) this long after its active phase ends, s.
    float ChainWindowSec = 0.3f;
    /// The longest chain, strikes: jab -> jab -> heavy is 3 (O.7).
    int MaxChainLength = 3;
    /// While blocking the fighter cannot step forward; it steps back at this
    /// share of the walking speed (the walk cycle plays backwards, slowly).
    float BlockBackSpeedScale = 0.3f;
    /// The move key released, a foot in the air: the walk cycle plays on to
    /// the nearest wide double support (both feet down, apart) this many
    /// times faster than walking (clip seconds per second) and the legs
    /// rest there.
    float WalkStopRate = 3.0f;
    /// A phase of the walk where both feet are down counts as a rest pose
    /// only if the ankles are at least this far apart (not where the feet
    /// pass each other), m.
    float RestMinFootSpread = 0.25f;
    /// The move key released mid-step: the pelvis may go on this far to
    /// finish the step on both feet (the legs follow its travel); a step that
    /// needs more ends short, the swing foot set down where it is, m.
    float StopMaxCoast = 0.06f;
    /// Into the pose of an action that needs the legs.
    LegStepTuning LegStep;
    /// The legs step when the pelvis moves faster than this, m/s: walking,
    /// a pelvis held slower than this by the opponent stops the walk cycle
    /// on both feet (no marching on the spot); not walking, a pelvis pushed
    /// faster than this makes the legs step along.
    float StepMinSpeed = 0.15f;
    /// Walking while crouched is this much slower than walking.
    float CrouchWalkSpeedScale = 0.5f;
    /// A strike other than the low kick pressed while crouched: the fighter
    /// stands up for this long before the strike starts, s.
    float CrouchStandUpSec = 0.12f;
    /// After the end of the fight the bodies keep moving this long without
    /// input, so that a knockout fall plays out, s.
    float EndSettleSec = 1.2f;
    /// \name A posed strike that runs into the opponent
    /// A kick into the legs or the pelvis stops there in any phase of the
    /// attack (Box2D does not collide two posed bodies, so nothing else would
    /// stop it), and so does one into a torso the solver cannot push away.
    /// @{
    /// How deep a posed striking limb may sink into the opponent's posed
    /// parts before it is stopped, m.
    float ContactStopDepth = 0.01f;
    /// The attack holds the contact pose this long before it recovers, s.
    float ContactHoldSec = 0.08f;
    /// The recovery after a held contact blends from the contact pose into
    /// the clip's recovery over this time, s.
    float ContactRecoveryBlendSec = 0.15f;
    /// @}
    /// The cross-overs between the clips (stance and walk legs included).
    BlendTable Blends;
};

/// Parses the tuning from JSON text. Every key is optional; an unknown key
/// is an error. Throws std::runtime_error.
CombatTuning parseCombatTuning(std::string_view JsonText);

/// Reads and parses a tuning file. Throws std::runtime_error.
CombatTuning loadCombatTuning(const std::filesystem::path& Path);

} // namespace fighter::combat
