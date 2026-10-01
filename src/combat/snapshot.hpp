//===- combat/snapshot.hpp - Fight state for the renderer -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines RenderSnapshot, the state of a fight as seen by the
/// renderer, and interpolate(), which blends two snapshots for smooth drawing
/// between physics steps (docs/DEVELOPMENT_PLAN.md, section 3.4).
///
/// The renderer reads only snapshots and knows nothing about physics.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/body.hpp"
#include "core/vec2.hpp"

namespace fighter::combat {

struct FighterView {
    Vec2 Position;                    ///< Reference point: midway between the feet, m.
    Vec2 Size{0.5f, 1.8f};            ///< Bounding size; in phase 0 a fighter is a rectangle.
    bool FacingRight = true;
    float Hp = 0.0f;
    float MaxHp = 0.0f;
    std::vector<PartTransform> Parts; ///< Body parts; empty until the rig exists (phase 1).
};

struct ArenaView {
    float HalfWidthM = 5.0f;          ///< Walls at x = +-HalfWidthM, floor at y = 0.
};

struct RenderSnapshot {
    std::uint64_t Tick = 0;
    double TimeLeftSec = 0.0;
    ArenaView Arena;
    std::array<FighterView, 2> Fighters;   ///< [0] is the left fighter, [1] the right one.
};

/// Returns the state between two steps for smooth drawing: \p Alpha = 0 gives
/// \p Prev, \p Alpha = 1 gives \p Curr. Discrete fields (HP, facing, tick) are
/// taken from \p Curr.
RenderSnapshot interpolate(const RenderSnapshot& Prev, const RenderSnapshot& Curr, float Alpha);

} // namespace fighter::combat
