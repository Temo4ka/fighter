//===- core/body.hpp - Shared body part vocabulary --------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the BodyPart enumeration and PartTransform, the world
/// placement of one body part.
///
/// They live in core because modules that must not depend on each other all
/// use them: stats (mass and armor per part), rig (the physical body), combat
/// (where a hit landed) and render (what to draw).
///
/// The set of parts may change after the physics spike (phase 1), but only
/// through review.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/vec2.hpp"

namespace fighter {

enum class BodyPart : std::uint8_t {
    Head,
    Torso,
    Pelvis,
    UpperArmL, ForearmL,
    UpperArmR, ForearmR,
    ThighL, ShinL, FootL,
    ThighR, ShinR, FootR,
    Count
};

inline constexpr std::size_t BodyPartCount = static_cast<std::size_t>(BodyPart::Count);

/// An array with one element per body part, indexed by BodyPart.
template <class T>
using PerBodyPart = std::array<T, BodyPartCount>;

constexpr std::string_view getBodyPartName(BodyPart Part) {
    constexpr std::array<std::string_view, BodyPartCount> Names = {
        "Head", "Torso", "Pelvis",
        "UpperArmL", "ForearmL", "UpperArmR", "ForearmR",
        "ThighL", "ShinL", "FootL", "ThighR", "ShinR", "FootR",
    };
    const auto I = static_cast<std::size_t>(Part);
    return I < Names.size() ? Names[I] : "?";
}

/// World placement of one body part. This is all the renderer knows about the
/// physical body.
struct PartTransform {
    BodyPart Part = BodyPart::Torso;
    Vec2 Position;       ///< Center of the part, m.
    float Angle = 0.0f;  ///< Radians, counter-clockwise.
};

} // namespace fighter
