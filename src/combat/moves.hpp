//===- combat/moves.hpp - Strikes described in data -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MoveDef, one strike as described in data/moves/*.json
/// (docs/DATA_FORMATS.md), and the functions that read it.
///
/// A move says what the strike does, not how it is started: its clip
/// (data/poses/<clip>.json), damage, the weakest reaction it causes, stamina,
/// a shorter clip for close range, the moves a hit can be chained into, its
/// tags and its intent for the AI. Which input starts it, and for which
/// weapon, is the business of a moveset (combat/moveset.hpp), so one move can
/// serve several weapons.
///
/// \code
///   // data/moves/jab.json; the id is the file stem
///   {"clip": "jab", "damage": 0.6, "min_reaction": "Touch", "stamina": 5,
///    "close_clip": "jab_close", "close_range_m": 0.6,
///    "chain_to": ["jab", "heavy_punch"],
///    "tags": ["high", "punch"],
///    "ai": {"range_m": [0.6, 0.9], "role": "poke"}}
///
///   // data/moves/sword_slash.json
///   {"clip": "sword_slash", "uses_weapon": true, "damage": 1.4,
///    "min_reaction": "Flinch", "stamina": 14, "tags": ["mid", "swing"]}
/// \endcode
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "combat/events.hpp"

namespace fighter::combat {

/// The height tags: where a strike lands, which decides the block zone that
/// stops it (decision O.2). A move has at most one.
inline constexpr std::array<std::string_view, 3> HeightTags = {"high", "mid", "low"};

/// What a move is for, so the AI (task 2.9) can choose it. Data only: the
/// battle itself does not read it.
struct MoveIntent {
    /// The pelvis distance the move is meant for, m; 0 and 0: not given.
    float MinRangeM = 0.0f;
    float MaxRangeM = 0.0f;
    /// "poke", "heavy", "close", "low", "anti_close"... (docs/DATA_FORMATS.md).
    std::string Role;
    float Weight = 1.0f;   ///< How often the AI picks it among moves of one role.
};

struct MoveDef {
    std::string Id;                    ///< The file stem: data/moves/<Id>.json.
    std::string Clip;                  ///< data/poses/<Clip>.json.
    /// Is the strike made with the weapon? Then the weapon's speed and power
    /// scales apply (WeaponProps); an unarmed strike of an armed fighter
    /// (a kick) keeps the fighter's own.
    bool UsesWeapon = false;
    float Damage = 1.0f;               ///< Multiplies the damage of a hit (O.4).
    /// The weakest reaction a clean (unblocked) hit causes, whatever its
    /// strength: a jab always at least touches.
    ReactionLevel MinReaction = ReactionLevel::None;
    float Stamina = 0.0f;              ///< Spent when the move starts (O.13).
    /// Played instead of Clip when the fighters' pelvises are closer than
    /// CloseRangeM when the move starts; empty: Clip at any range.
    std::string CloseClip;
    float CloseRangeM = 0.0f;
    /// The moves that may cancel the recovery of this move after it hit (a
    /// short chain, O.7), by id: see CombatTuning::ChainWindowSec.
    std::vector<std::string> ChainTo;
    /// Free-form labels: a height (HeightTags), a kind ("punch", "kick",
    /// "swing", "thrust"...) and anything reactions or the AI look for.
    std::vector<std::string> Tags;
    std::optional<MoveIntent> Intent;

    /// The clip to play when the opponent's pelvis is \p DistanceM away.
    const std::string& getClip(float DistanceM) const {
        return !CloseClip.empty() && DistanceM < CloseRangeM ? CloseClip : Clip;
    }
    bool canChainTo(std::string_view NextId) const;
    bool hasTag(std::string_view Tag) const;
    /// The height tag of the move, or an empty view if it has none.
    std::string_view getHeight() const;
};

/// Parses a move from JSON text. Throws std::runtime_error that names the
/// field.
MoveDef parseMoveDef(std::string_view JsonText, std::string Id);

/// Reads every *.json file in \p Dir (in name order); each file is one move
/// named after its stem. Throws std::runtime_error that names the file, also
/// if chain_to names a move that is not there.
std::vector<MoveDef> loadMoves(const std::filesystem::path& Dir);

/// The move with this id, or nullptr.
const MoveDef* findMoveById(std::span<const MoveDef> Moves, std::string_view Id);

} // namespace fighter::combat
