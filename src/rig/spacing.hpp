//===- rig/spacing.hpp - Spacing of two fighters and the walls --*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the spacing of two fighters (task 2.1): how close their
/// bodies may get to each other and to the arena walls.
///
/// The pelvises are kinematic, and Box2D never collides two kinematic
/// bodies, so this code is their "collision". Every step, after both rigs
/// planned their motion (Rig::planMotion) and before Rig::applyControl,
/// the battle calls keepApart():
///  - each pelvis stays BodyHalfWidth away from the walls; a fighter at a
///    wall touches it (Rig::getWallSide, Rig::isAgainstWall), and knockback
///    into the wall stops there;
///  - two standing fighters keep their pelvises 2 * BodyHalfWidth apart (at
///    SeparationSpeed at most), and their bodies off each other: the posed
///    parts (the legs) as they will stand after the step, the torsos and
///    heads (Rig::predictBody), at PosedSeparationSpeed at most. Box2D does
///    not collide posed parts, and a torso held on a posed pelvis cannot get
///    out of the way. The overlap is split by mass (the heavier one gives
///    way less); a fighter at a wall cannot give way. The push moves the
///    whole body, planted feet included (Rig::pushBody). The arms are left
///    out (the solver keeps them apart, and a stuck one yields). The
///    striking parts of an attack overlap only where they are and where the
///    clip takes them alike: their own motion into the opponent stops there
///    (Rig::stopAtContact), the opponent's into them is kept off here. What
///    the spacing cannot keep apart in time (a leg swung through the
///    opponent's in one step) Rig::holdLimbsBack stops;
///  - a standing fighter keeps its pelvis BodyHalfWidth + lyingClearance
///    (rig file) away from the body of a fighter lying on the floor, so its
///    legs do not walk through it.
///
/// When a strike lands, the battle calls pushApartOnHit(): a hit at close
/// range pushes the fighters apart to the rig's closeRange, so that arms
/// and torsos do not stay pressed into each other.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "rig/rig.hpp"

namespace fighter::rig {

/// The battle's spacing parameters (data/combat.json and the arena).
struct SpacingParams {
    /// The inner faces of the arena walls are at +-ArenaHalfWidth, m.
    float ArenaHalfWidth = 5.0f;
    /// Half the width of a fighter's pushbox, m: the pelvises stay at least
    /// twice this apart and this far from the walls.
    float BodyHalfWidth = 0.25f;
    /// A standing pelvis steps off a body lying under it at most this fast,
    /// m/s.
    float SeparationSpeed = 4.0f;
    /// The largest correction of a step the spacing looks for (overlapping
    /// pelvises or bodies), as a speed, m/s.
    float PosedSeparationSpeed = 12.0f;
    /// Beyond taking back their approach, the fighters are pushed apart at
    /// most this fast, m/s...
    float PushMaxSpeed = 1.5f;
    /// ...and that speed changes at most this fast, m/s^2 (eased in and out).
    float PushAcceleration = 20.0f;
    /// Bodies the eased push would leave deeper than this in each other are
    /// pushed apart at once, as far as that needs (a hard push), m.
    float MaxSoftOverlap = 0.008f;
};

/// Corrects the planned pelvis motion of both fighters for the walls and
/// for each other (see the file comment) and updates their wall contact.
/// Call once per step after Rig::planMotion of both and before
/// Rig::applyControl. The fighters may stand in either order.
void keepApart(Rig& First, Rig& Second, const SpacingParams& Params, float Dt);

/// A strike of \p Attacker landed on \p Victim. If their pelvises are closer
/// than the victim's ControlParams::CloseRange and both stand, both get a
/// push (Rig::addPush) that takes them apart to that range: split by mass,
/// and all of it to the attacker when the victim's back is at the wall.
/// Combat calls it for every landed strike, blocked or not, after
/// Rig::applyHit. Returns the distance the fighters will move apart, m.
float pushApartOnHit(Rig& Attacker, Rig& Victim);

} // namespace fighter::rig
