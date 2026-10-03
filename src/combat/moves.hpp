//===- combat/moves.hpp - Strikes described in data -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MoveDef, one strike as described in data/moves/*.json
/// (docs/DEVELOPMENT_PLAN.md, task 2.0.3; docs/DATA_FORMATS.md), the
/// functions that read it and findMove(), which picks the move a button
/// starts.
///
/// A move names its button and its clip (data/poses/<clip>.json) and adds
/// what the clip does not know: damage, the weakest reaction it causes,
/// stamina and the speed floor. A move with "weapon" belongs to that weapon
/// class (WeaponProps::Class) and replaces the unarmed move on its button
/// while such a weapon is held (decision O.12).
///
/// \code
///   // data/moves/jab.json; the id is the file stem
///   {"button": "Jab", "clip": "jab", "damage": 0.6, "min_reaction": "Touch",
///    "stamina": 5, "min_startup_sec": 0.15}
///
///   // data/moves/sword_slash.json
///   {"button": "HeavyPunch", "clip": "sword_slash", "weapon": "sword",
///    "damage": 1.4, "min_reaction": "Flinch", "stamina": 14,
///    "min_startup_sec": 0.3}
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

#include "combat/commands.hpp"
#include "combat/events.hpp"

namespace fighter::combat {

/// The attack buttons of PlayerCommands (decision O.1).
enum class MoveButton : uint8_t { Jab, HeavyPunch, BodyKick, LowKick, Count };

inline constexpr size_t MoveButtonCount = static_cast<size_t>(MoveButton::Count);

struct MoveDef {
    std::string Id;                    ///< The file stem: data/moves/<Id>.json.
    MoveButton Button = MoveButton::Jab;
    std::string Clip;                  ///< data/poses/<Clip>.json.
    /// The weapon class the move needs (WeaponProps::Class); empty for an
    /// unarmed move, which any fighter can use.
    std::string Weapon;
    float Damage = 1.0f;               ///< Multiplies the damage of a hit (O.4).
    /// The weakest reaction a clean (unblocked) hit causes, whatever its
    /// strength: a jab always at least touches.
    ReactionLevel MinReaction = ReactionLevel::None;
    float Stamina = 0.0f;              ///< Spent when the move starts (O.13).
    /// However fast the fighter, the striking phase starts no sooner than
    /// this after the button, s (O.7).
    float MinStartupSec = 0.0f;
};

/// Is the button of \p Button held in \p Cmd?
constexpr bool isPressed(const PlayerCommands& Cmd, MoveButton Button) {
    switch (Button) {
        case MoveButton::Jab: return Cmd.Jab;
        case MoveButton::HeavyPunch: return Cmd.HeavyPunch;
        case MoveButton::BodyKick: return Cmd.BodyKick;
        case MoveButton::LowKick: return Cmd.LowKick;
        case MoveButton::Count: break;
    }
    return false;
}

constexpr std::string_view getMoveButtonName(MoveButton Button) {
    constexpr std::array<std::string_view, MoveButtonCount> Names = {"Jab", "HeavyPunch", "BodyKick", "LowKick"};
    const auto Index = static_cast<size_t>(Button);
    return Index < Names.size() ? Names[Index] : "?";
}

/// The move \p Button starts for a fighter holding a weapon of class
/// \p WeaponClass (empty: no weapon): the weapon's own move on that button
/// if there is one, otherwise the unarmed move, otherwise nullptr.
const MoveDef* findMove(std::span<const MoveDef> Moves, MoveButton Button, std::string_view WeaponClass);

/// Parses a move from JSON text. Throws std::runtime_error that names the
/// field.
MoveDef parseMoveDef(std::string_view JsonText, std::string Id);

/// Reads every *.json file in \p Dir (in name order); each file is one move
/// named after its stem. Throws std::runtime_error that names the file, also
/// if two unarmed moves, or two moves of one weapon class, share a button.
std::vector<MoveDef> loadMoveSet(const std::filesystem::path& Dir);

} // namespace fighter::combat
