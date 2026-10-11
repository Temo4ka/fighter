//===- editor/catalog.hpp - What the pose editor can open -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the lists behind the "Open" panel of the pose editor:
/// the fighters of data/fighters with the items they hold in their hands and
/// the stance clip of their moveset, the items a hand can hold, the clips of
/// data/poses, who uses a stance clip, and which moves play a clip.
///
/// A fighter's stance is found the way the game finds it (findStance(): the
/// items of both hands choose the moveset, the moveset names the stance), not
/// by a rule of the editor, so that opening a fighter shows the stance that
/// fighter stands in.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "stats/equipment.hpp"

namespace fighter::editor {

/// A file of data/fighters, resolved.
struct FighterEntry {
    std::string File;                         ///< The file stem: data/fighters/<File>.json.
    std::string Name;                         ///< The fighter's name.
    /// The items held in the hands (a weapon or a shield), in the order of
    /// the sheet, as the sheet gives them; what the editor puts in the hands.
    std::vector<stats::ItemRef> HandItems;
    std::string MoveSetId;                    ///< The moveset the fighter plays with.
    std::string StanceClip;                   ///< Its idle stance, data/poses/<StanceClip>.json.
};

/// An item a hand can hold.
struct HandItemEntry {
    std::string Id;
    std::string Name;
    bool Shield = false;
    bool TwoHanded = false;
};

/// Who uses a stance clip.
struct StanceUsers {
    std::vector<std::string> Fighters;        ///< Names of the fighters that stand in it.
    std::vector<std::string> MoveSets;        ///< Ids of the movesets whose stance it is (also by inheritance).
    bool isEmpty() const { return Fighters.empty() && MoveSets.empty(); }
};

/// A move that plays a clip.
struct MoveCandidate {
    std::string MoveId;
    bool Close = false;                       ///< The clip is the move's close_clip.
    bool operator==(const MoveCandidate&) const = default;
};

/// Every fighter of \p DataDir / "fighters", in file name order. Throws
/// std::runtime_error that names the file when a sheet or the data it refers
/// to is broken.
std::vector<FighterEntry> listFighters(const std::filesystem::path& DataDir);

/// The items of \p DataDir / "items" that a hand holds (weapons and
/// shields), in the order of the catalog.
std::vector<HandItemEntry> listHandItems(const std::filesystem::path& DataDir);

/// The stems of the files of \p DataDir / "poses", sorted.
std::vector<std::string> listClips(const std::filesystem::path& DataDir);

/// Who uses the clip \p ClipName as an idle stance: the fighters of
/// listFighters() and the movesets of data/movesets (a moveset that inherits
/// a stance counts).
StanceUsers findStanceUsers(const std::filesystem::path& DataDir, const std::string& ClipName);

/// The moves of data/moves whose "clip" or "close_clip" is \p ClipName, in
/// the order of the move library.
std::vector<MoveCandidate> findMovesPlayingClip(const std::filesystem::path& DataDir, const std::string& ClipName);

} // namespace fighter::editor
