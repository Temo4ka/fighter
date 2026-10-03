//===- combat/result.hpp - The outcome of a fight ---------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines BattleResult, what a finished fight hands back to the
/// game that started it (docs/DEVELOPMENT_PLAN.md, task 2.0.4, decision O.6):
/// the winner, the health left, the hits taken per body part and the strike
/// statistics.
///
/// A fight is one round of 90 s (decision O.5): it ends with a knockout (HP
/// reaches 0) or when the time is up, and then the fighter with more HP wins.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "core/body.hpp"

namespace fighter::combat {

enum class Winner : uint8_t { Left, Right, Draw };

/// How the fight ended.
enum class BattleEnd : uint8_t {
    Knockout,   ///< A fighter's HP reached 0.
    TimeUp,     ///< The round time ran out; more HP wins, equal HP is a draw.
};

/// One fighter's use of one move.
struct StrikeStats {
    uint32_t Thrown = 0;    ///< Started.
    uint32_t Landed = 0;    ///< Hit the opponent, blocked or not.
    uint32_t Blocked = 0;   ///< Landed in a blocked zone (included in Landed).
    float Damage = 0.0f;    ///< Dealt in total.
};

/// The hits one body part took.
struct PartReport {
    uint32_t Hits = 0;
    float Damage = 0.0f;
};

struct FighterReport {
    float Hp = 0.0f;                   ///< Left at the end; the game carries it on.
    float DamageDealt = 0.0f;
    float DamageTaken = 0.0f;
    PerBodyPart<PartReport> HitsTaken{};
    /// Strikes of this fighter by move id (data/moves/<id>.json).
    std::map<std::string, StrikeStats, std::less<>> Moves;
    uint32_t Knockdowns = 0;           ///< Times this fighter was knocked down.
};

struct BattleResult {
    Winner WinnerSide = Winner::Draw;
    BattleEnd End = BattleEnd::TimeUp;
    double TimeSec = 0.0;
    std::array<FighterReport, 2> Fighters;   ///< [0] is the left fighter, [1] the right one.
};

} // namespace fighter::combat
