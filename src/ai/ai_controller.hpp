//===- ai/ai_controller.hpp - The simple AI ---------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares AiController, a Controller that plays a fighter with a
/// small state machine (task 2.9; the rules are in docs/MECHANICS.md):
///
///   Approach: walk towards the opponent until a strike is in range;
///   Hold:     stand in range, back off when too close, and every
///             AiConfig::DecisionIntervalSec decide whether to strike;
///   Attack:   hold the input of the chosen strike for AiConfig::PressSec;
///   Block:    guard the zone the opponent's strike aims at, noticed
///             AiConfig::ReactionDelaySec after it starts;
///   Recover:  do nothing until the strike is over and after a hit.
///
/// It reads only the snapshot (positions, state, phase, move id) and the moves
/// of its Repertoire. The only randomness is a generator in its own state
/// (AiConfig::Seed), so the same snapshots give the same commands.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ai/ai_config.hpp"
#include "ai/controller.hpp"
#include "ai/repertoire.hpp"

namespace fighter::ai {

enum class AiState : uint8_t { Approach, Hold, Attack, Block, Recover };

std::string_view getAiStateName(AiState State);

class AiController : public Controller {
public:
    /// \p Side is the index of the fighter it plays in RenderSnapshot::Fighters.
    AiController(size_t NewSide, AiConfig NewConfig, Repertoire NewRepertoire);

    combat::PlayerCommands getCommands(const combat::RenderSnapshot& State, double Dt) override;
    std::string_view getName() const override { return "ai"; }

    AiState getState() const { return Current; }
    /// The strike being performed or chosen last, or nullptr before the first.
    const AiMove* getChosenMove() const { return Chosen ? &Moves.Attacks[*Chosen] : nullptr; }
    /// Is a block planned or raised against the strike the opponent is making?
    bool isBlockPlanned() const { return BlockPlanned; }
    const AiConfig& getConfig() const { return Config; }

    /// The next number of the generator in [0, 1). Public for tests.
    float rollChance();

private:
    /// Moves to the next state if its conditions hold; \p Me is the AI's own view.
    void advance(const combat::FighterView& Me, float DistanceM);
    void enter(AiState Next);
    /// Watches the opponent's strikes; returns true if the AI blocks now.
    bool shouldBlock(const combat::FighterView& Opponent, float DistanceM, double Dt);
    /// Picks, by weight, one of the strikes in range now.
    std::optional<size_t> pickAttack(float DistanceM);
    /// Is a strike in range with \p MarginM to spare at its far end?
    bool hasAttackInRange(float DistanceM, float MarginM) const;
    /// The strike with the shortest reach (for a fighter backed to a wall).
    std::optional<size_t> findClosestAttack() const;
    float getFarthestReachM() const;
    float getNearestReachM() const;
    void publish(const combat::RenderSnapshot& State, float DistanceM) const;

    size_t Side;
    AiConfig Config;
    Repertoire Moves;

    AiState Current = AiState::Approach;
    double StateSec = 0.0;        ///< How long in the current state.
    std::optional<size_t> Chosen;   ///< Index into Moves.Attacks.
    // The opponent's strike being watched.
    bool Watching = false;
    std::string WatchedMove;
    double WatchedSec = 0.0;
    bool BlockPlanned = false;
    double SinceStrikeSec = 1.0e9;   ///< Since the opponent last made a strike.
    std::string BlockHeight = "mid";
    uint64_t RngState;
};

} // namespace fighter::ai
