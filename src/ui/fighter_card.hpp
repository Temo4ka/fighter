//===- ui/fighter_card.hpp - What a fighter card shows ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares FighterCard, the summary of a fighter sheet for the
/// selection screen, and its loader.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

namespace fighter::ui {

struct FighterCard {
    std::string Name;     ///< "name" of the sheet; the file name if the sheet is broken.
    std::string Weapon;   ///< Display name of the weapon item, "Unarmed" without one.
    int Strength = 0;
    int Dexterity = 0;
    int Constitution = 0;
    std::string Error;    ///< Empty when the sheet is fine.
};

/// Reads data/fighters/<FileName>.json and data/items. Never throws: a broken
/// sheet gives a card with Error set, which the screen shows.
FighterCard loadFighterCard(const std::filesystem::path& DataDir, const std::string& FileName);

} // namespace fighter::ui
