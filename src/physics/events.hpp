//===- physics/events.hpp - Physics events for combat -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the physics -> combat contract (docs/DEVELOPMENT_PLAN.md,
/// section 4): HitEvent, emitted when one body part strikes another,
/// PartRef, which names a body part of a particular fighter, and
/// PartOverlap, how deep parts of two fighters sink into each other.
///
/// The contract changes only through review.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "core/body.hpp"
#include "core/vec2.hpp"

namespace fighter::physics {

/// A body part of a particular fighter.
struct PartRef {
    uint8_t Fighter = 0;   ///< 0 is the left fighter, 1 is the right one.
    BodyPart Part = BodyPart::Torso;
};

/// One body part hit another. Damage is computed by combat from the impulse
/// and the armor of the victim's body part.
struct HitEvent {
    PartRef Attacker;
    PartRef Victim;
    Vec2 Point;                 ///< Contact point, m.
    float ApproachSpeed = 0.0f; ///< Closing speed at the contact point before the step, m/s.
    /// The reduced mass of the two parts' strike masses, mA*mB/(mA+mB), kg.
    float StrikeMass = 0.0f;
    /// ApproachSpeed * StrikeMass: the impulse of a perfectly inelastic
    /// collision of the two strike masses, N*s (physics::World).
    float Impulse = 0.0f;
};

/// The deepest overlap of two body parts of different fighters
/// (World::findDeepestOverlap).
struct PartOverlap {
    PartRef First;
    PartRef Second;
    float Depth = 0.0f;   ///< How far the surfaces sink into each other, m.
    Vec2 Point;           ///< Midway between the surfaces.
};

} // namespace fighter::physics
