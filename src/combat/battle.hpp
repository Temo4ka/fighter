//===- combat/battle.hpp - Public API of the combat module ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the public API of the combat module
/// (docs/DEVELOPMENT_PLAN.md, section 4): Battle, which runs one fight. The
/// configuration, the events and the result have headers of their own
/// (config.hpp, events.hpp, result.hpp); this one includes them all.
///
/// An external project creates a Battle from a configuration, feeds it input
/// every step and reads the result. The contracts change only through review.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "combat/commands.hpp"
#include "combat/config.hpp"
#include "combat/events.hpp"
#include "combat/result.hpp"
#include "combat/snapshot.hpp"

namespace fighter::combat {

/// Runs one fight.
///
/// Phase 1.5 (hybrid body): each fighter is a rig::Rig in a Box2D world whose
/// pelvis and legs are moved by code and whose upper body is physical; it
/// walks, jabs and kicks. A hit sways the victim's upper body, knocks it back
/// (impulse / mass) and weakens its motors for a moment; a strong one knocks
/// it down. There is no damage, blocking or knockout yet: that is the state
/// machine of phase 2 (task 2.3). The interface stays.
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
    /// Set once the fight is over; update() does nothing after that.
    const std::optional<BattleResult>& getResult() const { return Result; }
    /// What happened during the last update(), in order. A landed strike is
    /// a contact of a striking limb in the active phase of an attack, at most
    /// one per attack; bumps are not events.
    std::span<const BattleEvent> getEvents() const { return Events; }
    const BattleConfig& getConfig() const { return Cfg; }

private:
    struct Simulation;

    void finish(Winner Outcome, BattleEnd End);
    void publishSnapshot();
    void drawDebug() const;

    BattleConfig Cfg;
    std::unique_ptr<Simulation> Sim;   ///< Physics world, fighters, clips.
    double ElapsedSec = 0.0;
    uint64_t Tick = 0;
    RenderSnapshot Snapshot;
    std::vector<BattleEvent> Events;
    std::array<FighterReport, 2> Reports;   ///< Collected during the fight.
    std::optional<BattleResult> Result;
};

} // namespace fighter::combat
