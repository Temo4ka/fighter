//===- combat/battle.hpp - Public API of the combat module ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the public API of the combat module
/// (docs/DEVELOPMENT_PLAN.md, section 4): the fight configuration, its result
/// and Battle, which runs one fight.
///
/// An external project creates a Battle from a configuration, feeds it input
/// every step and reads the result. The contracts change only through review.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "combat/commands.hpp"
#include "combat/snapshot.hpp"
#include "core/vec2.hpp"
#include "stats/stats.hpp"

namespace fighter::combat {

struct FighterConfig {
    stats::Stats Stats;
    stats::Loadout Loadout;
    std::string RigId = "humanoid";
};

struct ArenaConfig {
    float HalfWidthM = 5.0f;
    Vec2 Gravity{0.0f, -9.81f};
};

struct BattleConfig {
    FighterConfig Left;
    FighterConfig Right;
    ArenaConfig Arena;
    double RoundTimeSec = 90.0;
};

enum class Winner { Left, Right, Draw };

struct FighterReport {
    float HpLeft = 0.0f;
    float DamageDealt = 0.0f;
};

struct BattleResult {
    Winner WinnerSide = Winner::Draw;
    double TimeSec = 0.0;
    FighterReport Left;
    FighterReport Right;
};

/// Runs one fight.
///
/// PLACEHOLDER for phase 0: fighters are kinematic rectangles (walking,
/// jumping, walls) and cannot strike. It exists to test input, the loop, the
/// renderer and the debug layer. In phases 1-2 the internals are replaced with
/// the rig and physics; the interface stays.
class Battle {
public:
    explicit Battle(const BattleConfig& Config);

    void update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt);

    const RenderSnapshot& getSnapshot() const { return Snapshot; }
    std::optional<BattleResult> getResult() const { return Result; }
    const BattleConfig& getConfig() const { return Cfg; }

private:
    struct FighterState {
        Vec2 Position;
        Vec2 Velocity;
        bool Grounded = true;
        bool FacingRight = true;
        float Hp = 0.0f;
        stats::PhysicalProfile Profile;
    };

    void updateFighter(FighterState& Fighter, const PlayerCommands& Cmd, float Dt);
    void finish(Winner Outcome);
    void publishSnapshot();
    void drawDebug() const;

    BattleConfig Cfg;
    std::array<FighterState, 2> Fighters;
    double ElapsedSec = 0.0;
    uint64_t Tick = 0;
    RenderSnapshot Snapshot;
    std::optional<BattleResult> Result;
};

} // namespace fighter::combat
