//===- stats/loading.hpp - Stats, items and fighters from JSON --*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the functions that read stats, the item catalog and
/// fighter sheets from JSON (data/items/*.json, data/fighters/*.json).
///
/// Every parse function takes the text itself, so tests need no files, and a
/// source name that starts every error message. Every load function reads a
/// file and passes its path as the source name. Errors are DataError with
/// the file, the field and the offending value in the message, for example
/// "data/items/armor.json: items[1]: field 'mass_kg': expected a number, got
/// string".
///
/// Unknown fields are errors too: a typo in a key does not silently fall back
/// to a default. Slot and body part names are written exactly as the enum
/// values (getEquipmentSlotName(), getBodyPartName()).
///
/// \code
///   // data/items/armor.json
///   {"items": [
///     {"id": "iron_helmet", "name": "Iron helmet", "slot": "Head",
///      "covers": ["Head"], "mass_kg": 2.5, "armor": 0.3}
///   ]}
///
///   // data/items/weapons.json: "weapon" only in the Weapon slot
///   {"items": [
///     {"id": "short_sword", "slot": "Weapon", "covers": ["ForearmR"],
///      "mass_kg": 1.2, "armor": 0.0,
///      "weapon": {"moveset": "sword", "reach_m": 0.55,
///                 "speed_scale": 1.0, "power_scale": 1.2}}
///   ]}
///
///   // data/fighters/knight.json
///   {"name": "Knight",
///    "stats": {"strength": 14, "dexterity": 8, "constitution": 14},
///    "items": ["iron_helmet", "chainmail"]}
/// \endcode
///
/// The balance table, data/balance.json (docs/DATA_FORMATS.md):
/// \code
///   {"base_mass_kg": {"Head": 5.0, "Torso": 26.0, ...all 13 parts...},
///    "mass_per_con": 0.03, "base_motor_torque": 150.0, ...}
/// \endcode
/// Every field is required and an unknown one is an error; the values are
/// checked by validateBalanceTable().
///
/// "name" of an item is optional and defaults to its id; "weapon" is optional
/// (an item in the Weapon slot without it is not a weapon yet); every other
/// field is required, including all four fields of "weapon".
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include "stats/equipment.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/stats.hpp"

namespace fighter::stats {

/// Default source name for text that does not come from a file.
inline constexpr std::string_view InlineSourceName = "<inline>";

/// Parses {"strength": 12, "dexterity": 10, "constitution": 11}. All three
/// fields are required integers; the values are checked by validateStats().
Stats parseStats(std::string_view Text, std::string_view SourceName = InlineSourceName);

/// Parses an item file, {"items": [...]}, into a new catalog. Every item is
/// checked by validateItem(); ids must be unique.
ItemCatalog parseItemCatalog(std::string_view Text, std::string_view SourceName = InlineSourceName);

/// Loads one item file or, if Path is a directory, every *.json file in it
/// (in name order) into one catalog. Ids must be unique across all files.
ItemCatalog loadItemCatalog(const std::filesystem::path& Path);

/// Parses a fighter sheet, {"name": ..., "stats": {...}, "items": [...]}.
/// The stats are checked here; item ids are checked by resolveFighterSheet().
FighterSheet parseFighterSheet(std::string_view Text, std::string_view SourceName = InlineSourceName);

/// Loads a fighter sheet from a file.
FighterSheet loadFighterSheet(const std::filesystem::path& Path);

/// Parses data/balance.json into a BalanceTable.
BalanceTable parseBalanceTable(std::string_view Text, std::string_view SourceName = InlineSourceName);

/// Loads data/balance.json from a file.
BalanceTable loadBalanceTable(const std::filesystem::path& Path);

} // namespace fighter::stats
