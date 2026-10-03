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
///  - two standing fighters keep their pelvises 2 * BodyHalfWidth apart: the
///    overlap goes away at SeparationSpeed at most and is split by mass (the
///    heavier one gives way less); a fighter at a wall cannot give way;
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
    /// Overlapping pelvises are pushed apart at most this fast, m/s.
    float SeparationSpeed = 4.0f;
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
