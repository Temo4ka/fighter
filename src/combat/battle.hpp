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
#include <cstddef>
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

struct Surroundings;

/// Runs one fight.
///
/// Each fighter is a rig::Rig in a Box2D world whose pelvis and legs are
/// moved by code and whose upper body is physical, driven by a state machine
/// (combat/fighter.hpp): it walks, crouches, blocks by zone and strikes with
/// the moves of data/moves/. A strike that lands sways the victim's upper
/// body and knocks it back (impulse / mass); data/reactions.json turns it
/// into damage and a reaction level (stun, knockdown), lowered by a block in
/// the right zone (task 2.3). One round: a knockout or the time decides.
///
/// The physics, rig and clip types stay inside the implementation, so this
/// header does not pull them in.
class Battle {
public:
    /// Reads the rigs, clips, moves, reactions and tuning from Config.DataDir.
    /// Throws std::runtime_error if a file is missing or broken.
    explicit Battle(const BattleConfig& Config);
    ~Battle();

    Battle(Battle&& Other) noexcept;
    Battle& operator=(Battle&& Other) noexcept;

    void update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt);

    const RenderSnapshot& getSnapshot() const { return Snapshot; }
    /// Set once the fight is over and fixed from then on. After the end,
    /// update() lets the bodies move on without input for a moment
    /// (CombatTuning::EndSettleSec), so that a knockout fall plays out; it
    /// reports no events and changes nothing in the result.
    const std::optional<BattleResult>& getResult() const { return Result; }
    /// What happened during the last update(), in order. A landed strike is
    /// a contact of a striking limb in the active phase of an attack, at most
    /// one per attack; bumps are not events.
    std::span<const BattleEvent> getEvents() const { return Events; }
    const BattleConfig& getConfig() const { return Cfg; }
    /// The deepest overlap of a part of one fighter with a part of the
    /// other one now, whatever moves them (physics::World::findDeepestOverlap):
    /// nothing should pass through the opponent. Nullopt if nothing overlaps.
    std::optional<physics::PartOverlap> findDeepestOverlap() const;
    /// The overlap of two parts of different fighters now that goes furthest
    /// beyond what its pair may overlap (getOverlapTolerance). Nullopt if
    /// nothing overlaps.
    std::optional<physics::PartOverlap> findWorstOverlap() const;
    /// How deep the parts of \p Overlap may sink into each other, m: two
    /// arms pressed together by their motors CombatTuning::ArmOverlapTolerance,
    /// any other pair CombatTuning::OverlapTolerance (data/combat.json;
    /// rig::ContactResolver::getOverlapTolerance).
    float getOverlapTolerance(const physics::PartOverlap& Overlap) const;

private:
    struct Simulation;

    void finish(Winner Outcome, BattleEnd End);
    /// One step after the end: the bodies move, nothing else happens.
    void settle(float Dt);
    /// Where the opponent of fighter \p Index is.
    Surroundings getSurroundings(size_t Index) const;
    /// After the physics step: the posed limbs of both back to the contact
    /// (rig::ContactResolver::afterStep), and a stopped strike holds its
    /// clip (Fighter::onPosedStop).
    void stopPosedLimbs();
    void publishSnapshot();
    /// The panel and the debug draw, the contact stages and the overlap
    /// first (rig::ContactResolver::drawDebug). Does nothing in the release
    /// build.
    void drawDebug();

    BattleConfig Cfg;
    std::unique_ptr<Simulation> Sim;   ///< Physics world, fighters, clips.
    double ElapsedSec = 0.0;
    uint64_t Tick = 0;
    RenderSnapshot Snapshot;
    std::vector<BattleEvent> Events;
    std::array<FighterReport, 2> Reports;   ///< Collected during the fight.
    std::optional<BattleResult> Result;
    float SettleLeftSec = 0.0f;   ///< How long the bodies still move after the end, s.
};

} // namespace fighter::combat
