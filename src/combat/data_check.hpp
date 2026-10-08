//===- combat/data_check.hpp - Check every file of data/ --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares checkData(), which loads every file of a data
/// directory and checks the references between them (docs/DATA_FORMATS.md,
/// "Проверка данных"), and collects all the problems it finds instead of
/// stopping at the first: `fighter_app --check-data` prints them.
///
/// The files are read by the same loaders the battle uses, one file at a
/// time, so that a broken file does not hide the problems of the others.
/// What a loader checks inside one file it reports as it does at load time;
/// the checks across files are:
///
///   - fighters: the items exist and fit their slots (resolveFighterSheet);
///   - items: ids are unique across files, "moveset" names a moveset;
///   - movesets: inputs name moves, "inherit" names a set and does not come
///     back to the set, "pair" names two plain sets and no other set is for
///     the same pair (MoveLibrary::findProblems());
///   - moves: "chain_to" names moves, "clip" and "close_clip" have files in
///     poses/, the clip has an active phase and striking parts, and a move
///     made with the weapon strikes with the arm that holds it;
///   - the clips the state machine plays (stance, walk, the blocks and
///     reactions) exist, and so do the clips of the movesets' blocks.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <vector>

#include "combat/moveset.hpp"

namespace fighter::combat {

/// Every problem in the data directory \p DataDir (the directory with
/// balance.json, moves/, movesets/ ...), in the order of the files; the
/// file of a problem is relative to \p DataDir. Empty if the data is sound.
/// Does not throw for a data error.
std::vector<DataProblem> checkData(const std::filesystem::path& DataDir);

} // namespace fighter::combat
