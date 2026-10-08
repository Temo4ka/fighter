//===- combat/config.hpp - What a fight starts from -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines BattleConfig, what the game that starts a fight hands
/// to it (docs/DEVELOPMENT_PLAN.md, task 2.0.4, decision O.6): both
/// fighters' stats, equipment and starting HP, the arena and the round time.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/vec2.hpp"
#include "stats/stats.hpp"

namespace fighter::combat {

struct FighterConfig {
    std::string Name;                 ///< For the HUD, the log and the debug panel.
    stats::Stats Stats;
    stats::Loadout Loadout;
    /// HP at the start, for example after an earlier fight in the game.
    /// nullopt: full health (PhysicalProfile::MaxHp). Clamped to (0, MaxHp].
    std::optional<float> StartHp;
    std::string RigId = "humanoid";   ///< data/rigs/<RigId>.json.
};

struct ArenaConfig {
    float HalfWidthM = 5.0f;          ///< Walls at x = +-HalfWidthM.
    Vec2 Gravity{0.0f, -9.81f};
};

struct BattleConfig {
    FighterConfig Left;
    FighterConfig Right;
    ArenaConfig Arena;
    /// One round (decision O.5): when the time is up, more HP wins.
    double RoundTimeSec = 90.0;
    /// Distance between the fighters' pelvises at the start, m; nullopt: the
    /// tuning's spawnDistance (data/combat.json). The move stand sets it to
    /// put the dummy where the move reaches (combat/move_measure.hpp).
    std::optional<float> SpawnDistanceM;
    /// Directory with the battle data (rigs/, poses/, moves/, combat.json,
    /// reactions.json, balance.json). It is read when a Battle is created,
    /// so a new Battle picks up edited files (live tuning).
    std::filesystem::path DataDir = "data";
};

} // namespace fighter::combat
