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

#include <filesystem>
#include <string_view>

namespace fighter::combat {

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
    int PhysicsSteps = 4;
    int PhysicsSubSteps = 1;
    /// @}
    /// \name Allowed overlap of the fighters
    /// How deep a part of one fighter may overlap a part of the other one
    /// (Battle::findWorstOverlap, the "overlap" panel line, the invariant
    /// tests), m. Two arms pressed into each other by their motors sink in a
    /// little more than anything else.
    /// @{
    float ArmOverlapTolerance = 0.02f;
    float OverlapTolerance = 0.01f;
    /// @}
    /// Closing speed above which a contact between body parts of different
    /// fighters is reported by physics, m/s. Combat then keeps only the
    /// contacts of a striking limb in the active phase of an attack.
    float HitSpeedThreshold = 0.6f;
    /// Half the width of a fighter's pushbox, m: the pelvises stay at least
    /// twice this apart and this far from the arena walls.
    float BodyHalfWidth = 0.25f;
    /// Overlapping pelvises are pushed apart at most this fast, m/s.
    float SeparationSpeed = 4.0f;
    /// Overlapping bodies (legs, torsos, heads; rig/spacing.hpp) are pushed
    /// apart at most this fast, m/s.
    float PosedSeparationSpeed = 12.0f;
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
    /// the nearest phase with both feet down this many times faster than
    /// walking (clip seconds per second).
    float WalkStopRate = 3.0f;
    /// Stopped, the legs cross over from the walk cycle to the stance clip
    /// (or the switched stance) in this time; walking crosses back the same
    /// way, s.
    float StanceSettleSec = 0.15f;
    /// While a walk plays on to its stop and the legs settle into the
    /// stance, the planted foot slides along with the clip (true): the
    /// fighter ends in the exact stance, but that foot may slide up to about
    /// 20 cm. False: it stays where it stood, the leg bends to it (the rig's
    /// footLockSlip), and the stance comes out uneven.
    bool StopSlidesFeet = true;
    /// From the switched stance a jab or a kick (lead side left) steps the
    /// legs back into the normal stance during its startup: in this share of
    /// it (0..1]. The startup itself does not change.
    float SwitchStepShare = 0.8f;
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
};

/// Parses the tuning from JSON text. Every key is optional; an unknown key
/// is an error. Throws std::runtime_error.
CombatTuning parseCombatTuning(std::string_view JsonText);

/// Reads and parses a tuning file. Throws std::runtime_error.
CombatTuning loadCombatTuning(const std::filesystem::path& Path);

} // namespace fighter::combat
