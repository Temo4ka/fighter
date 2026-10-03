//===- combat/events.hpp - What happened during one step --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines BattleEvent, the things that happened during one step of
/// a fight (docs/DEVELOPMENT_PLAN.md, task 2.0.1): strikes started and
/// landed, knockdowns, the end of the fight. Battle::getEvents() returns the
/// events of the last update().
///
/// Events are for the ones that react to moments rather than to state: the
/// renderer (hit flash, camera shake, dust), sound, the menus, statistics and
/// the AI. The state itself is in RenderSnapshot.
///
/// The contract changes only through review.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "combat/result.hpp"
#include "physics/events.hpp"

namespace fighter::combat {

/// How strongly a fighter reacts to a hit, from the weakest. The strength of
/// a hit (impulse / mass x location x (1 - armor), m/s) and the thresholds in
/// data/reactions.json choose the level; the victim's poise raises them.
enum class ReactionLevel : uint8_t {
    None,
    Touch,       ///< A visible touch, no interruption.
    Flinch,      ///< Interrupts an attack.
    Stagger,     ///< A step back, cannot act for a moment.
    Knockback,   ///< Thrown back; stopped by a wall.
    Knockdown,   ///< Falls (ragdoll) and gets up.
};

/// A fighter started a move: an attack button was accepted.
struct StrikeStarted {
    uint8_t Fighter = 0;      ///< 0 is the left fighter, 1 the right one.
    std::string MoveId;       ///< data/moves/<MoveId>.json.
};

/// A strike hit the opponent: at most one per started move.
struct StrikeLanded {
    physics::HitEvent Contact;   ///< Who hit whom with what, where and how hard.
    std::string MoveId;
    float Strength = 0.0f;       ///< m/s: what the damage and the reaction are computed from (O.4).
    float Damage = 0.0f;         ///< HP taken, after the block and the armor.
    ReactionLevel Reaction = ReactionLevel::None;
    bool Blocked = false;        ///< It landed in the zone the victim blocks.
};

/// A fighter fell. Followed by GotUp, unless the fight ends first.
struct KnockedDown {
    uint8_t Fighter = 0;
};

/// A knocked-down fighter is on its feet again and can act.
struct GotUp {
    uint8_t Fighter = 0;
};

/// A fighter's stamina reached zero: it is slower until it recovers (O.13).
struct Exhausted {
    uint8_t Fighter = 0;
};

/// The fight is over; Battle::getResult() has the details.
struct BattleOver {
    Winner WinnerSide = Winner::Draw;
    BattleEnd End = BattleEnd::TimeUp;
};

using BattleEvent = std::variant<StrikeStarted, StrikeLanded, KnockedDown, GotUp, Exhausted, BattleOver>;

} // namespace fighter::combat
