//===- ai/repertoire.hpp - What the AI can strike with ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares Repertoire, the strikes a fighter's moveset gives to the
/// AI, with the input that starts each one and the range, role and weight from
/// the "ai" block of the move (docs/DATA_FORMATS.md, moves). It also knows the
/// height tag (high, mid, low) of every move in the data, so the AI can pick
/// the block zone against the opponent's strike.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ai/ai_config.hpp"
#include "combat/move_input.hpp"
#include "combat/moveset.hpp"
#include "stats/stats.hpp"

namespace fighter::ai {

/// One strike the AI may choose.
struct AiMove {
    std::string MoveId;
    combat::MoveInput Input;   ///< The direction and buttons that start it.
    float MinRangeM = 0.0f;    ///< The pelvis distances it is meant for.
    float MaxRangeM = 0.0f;
    std::string Role;          ///< From "ai.role"; empty if none.
    float Weight = 1.0f;       ///< How often it is picked among those in range.

    bool isInRange(float DistanceM) const { return DistanceM >= MinRangeM && DistanceM <= MaxRangeM; }
};

struct Repertoire {
    std::vector<AiMove> Attacks;
    /// The height tag of every move by id; a move without one is absent.
    std::map<std::string, std::string, std::less<>> Heights;

    /// "high", "mid" or "low" for a move id; "mid" if unknown.
    std::string_view getHeight(std::string_view MoveId) const;
};

/// The moveset a fighter with \p Gear fights with: the same choice the battle
/// makes (MoveLibrary::selectSet()).
const combat::MoveSet& selectMoveSet(const combat::MoveLibrary& Library, const stats::Loadout& Gear);

/// The strikes of \p Set (its own lines first, then its parents'; a line
/// whose input a nearer set already uses is skipped), with ranges taken from
/// the moves, or from \p Config for a move that gives none.
Repertoire buildRepertoire(const combat::MoveLibrary& Library, const combat::MoveSet& Set, const AiConfig& Config);

/// The commands that hold \p Input for a fighter that faces right or left
/// (the direction "Forward" is the way it faces).
combat::PlayerCommands makeAttackCommands(const combat::MoveInput& Input, bool FacingRight);

/// The commands that block a strike of the given height ("high", "mid",
/// "low"; anything else blocks mid).
combat::PlayerCommands makeBlockCommands(std::string_view Height);

} // namespace fighter::ai
