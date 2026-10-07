//===- stats/fighter_sheet.hpp - A fighter as written in data ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares FighterSheet, a fighter as described in
/// data/fighters/*.json (name, stats and item ids), and
/// resolveFighterSheet(), which turns it into Stats and a Loadout.
///
/// The combat module builds its FighterConfig from the result; stats does not
/// know about combat.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "stats/equipment.hpp"
#include "stats/stats.hpp"

namespace fighter::stats {

/// A fighter as written in a data file: items are referenced by id, a
/// one-handed item may name the hand it is held in.
struct FighterSheet {
    std::string Name;
    Stats BaseStats;
    std::vector<ItemRef> Items;
};

/// A fighter with its items looked up and everything checked.
struct ResolvedFighter {
    std::string Name;
    Stats BaseStats;
    Loadout Gear;
};

/// Checks the stats (validateStats()) and builds the loadout from the catalog
/// (buildLoadout()). Throws DataError whose message starts with the fighter
/// name.
ResolvedFighter resolveFighterSheet(const FighterSheet& Sheet, const ItemCatalog& Catalog);

} // namespace fighter::stats
