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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "combat/commands.hpp"
#include "combat/snapshot.hpp"
#include "core/vec2.hpp"
#include "physics/events.hpp"
#include "stats/stats.hpp"

namespace fighter::combat {

struct FighterConfig {
    stats::Stats Stats;
    stats::Loadout Loadout;
    std::string RigId = "humanoid";   ///< data/rigs/<RigId>.json.
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
    /// Directory with rigs/ and poses/. The data is read when a Battle is
    /// created, so a new Battle picks up edited files (live tuning).
    std::filesystem::path DataDir = "data";
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
/// Phase 1 (physics spike): each fighter is an active ragdoll (rig::Rig in a
/// Box2D world) that walks, jabs and kicks; hits make the victim's motors
/// weaker for a moment. There is no damage, blocking or knockout yet: that is
/// the state machine of phase 2. The interface stays.
///
/// The physics, rig and clip types stay inside the implementation, so this
/// header does not pull them in.
class Battle {
public:
    /// Reads the rigs and clips from Config.DataDir. Throws std::runtime_error
    /// if a file is missing or broken.
    explicit Battle(const BattleConfig& Config);
    ~Battle();

    Battle(Battle&& Other) noexcept;
    Battle& operator=(Battle&& Other) noexcept;

    void update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt);

    const RenderSnapshot& getSnapshot() const { return Snapshot; }
    std::optional<BattleResult> getResult() const { return Result; }
    /// Strikes that landed during the last update(): contacts of a striking
    /// limb in the active phase of an attack. Bumps are not included.
    std::span<const physics::HitEvent> getHits() const { return Hits; }
    const BattleConfig& getConfig() const { return Cfg; }

private:
    struct Simulation;

    void finish(Winner Outcome);
    void publishSnapshot();
    void drawDebug() const;

    BattleConfig Cfg;
    std::unique_ptr<Simulation> Sim;   ///< Physics world, fighters, clips.
    double ElapsedSec = 0.0;
    uint64_t Tick = 0;
    RenderSnapshot Snapshot;
    std::vector<physics::HitEvent> Hits;
    std::optional<BattleResult> Result;
};

} // namespace fighter::combat
