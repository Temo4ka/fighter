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
/// The renderer reads only snapshots and knows nothing about physics. The AI
/// reads them too: what the opponent does and where (task 2.0.1).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "combat/commands.hpp"
#include "combat/events.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"

namespace fighter::combat {

/// What a fighter is doing as a whole. Exactly one at a time.
enum class FighterState : uint8_t {
    Idle,          ///< In the stance, can act.
    Walking,
    Crouching,
    Attacking,     ///< FighterView::MoveId and FighterView::Phase say which and how far.
    Blocking,      ///< FighterView::Block says which zone.
    Reacting,      ///< Hit: FighterView::Reaction says how strongly; cannot act for a moment.
    KnockedDown,   ///< A ragdoll on the floor.
    GettingUp,
    KnockedOut,    ///< HP reached 0; the fight is over.
};

/// The phase of the attack being performed.
enum class AttackPhase : uint8_t {
    None,       ///< Not attacking.
    Startup,    ///< The wind-up before the striking phase: readable, can be blocked in time.
    Active,     ///< The striking limbs hit.
    Recovery,   ///< After the striking phase, until the next action.
};

struct FighterView {
    Vec2 Position;                    ///< Reference point: midway between the feet, m.
    Vec2 Size{0.5f, 1.8f};            ///< Bounding size; in phase 0 a fighter is a rectangle.
    bool FacingRight = true;
    float Hp = 0.0f;
    float MaxHp = 0.0f;
    float Stamina = 0.0f;             ///< Spent by strikes and blocks, recovers over time (O.13).
    float MaxStamina = 0.0f;

    FighterState State = FighterState::Idle;
    std::string MoveId;               ///< The move being performed (data/moves/), empty if none.
    AttackPhase Phase = AttackPhase::None;
    BlockZone Block = BlockZone::Mid; ///< Meaningful while State is Blocking.
    /// The reaction being played while State is Reacting, otherwise None.
    ReactionLevel Reaction = ReactionLevel::None;
    /// Back against an arena wall: cannot retreat, knockback is stopped (O.11).
    bool AgainstWall = false;

    std::vector<PartTransform> Parts; ///< Body parts; empty until the rig exists (phase 1).
};

struct ArenaView {
    float HalfWidthM = 5.0f;          ///< Walls at x = +-HalfWidthM, floor at y = 0.
};

struct RenderSnapshot {
    uint64_t Tick = 0;
    double TimeLeftSec = 0.0;
    ArenaView Arena;
    std::array<FighterView, 2> Fighters;   ///< [0] is the left fighter, [1] the right one.
};

/// Returns the state between two steps for smooth drawing: \p Alpha = 0 gives
/// \p Prev, \p Alpha = 1 gives \p Curr. Discrete fields (HP, stamina,
/// state, facing, tick) are taken from \p Curr.
RenderSnapshot interpolate(const RenderSnapshot& Prev, const RenderSnapshot& Curr, float Alpha);

} // namespace fighter::combat
