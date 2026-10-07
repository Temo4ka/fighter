//===- ui/fighter_list.hpp - Fighters available for selection ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares listFighters(), the names for the selection screen.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fighter::ui {

/// The names (file stems) of the *.json files in \p FightersDir, sorted. A
/// missing directory gives an empty list. The files are not parsed here: a
/// broken sheet is reported when the battle starts.
std::vector<std::string> listFighters(const std::filesystem::path& FightersDir);

} // namespace fighter::ui
