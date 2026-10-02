//===- physics/events.hpp - Physics events for combat -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the physics -> combat contract (docs/DEVELOPMENT_PLAN.md,
/// section 4): HitEvent, emitted when one body part strikes another, and
/// PartRef, which names a body part of a particular fighter.
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
    float ApproachSpeed = 0.0f; ///< Closing speed at the moment of impact, m/s.
    float Impulse = 0.0f;       ///< Contact impulse, N*s.
};

} // namespace fighter::physics
