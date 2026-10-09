//===- combat/moveset.hpp - Inputs to moves, and the block ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MoveSet, the strikes and the block of one weapon (or
/// of bare hands) as described in data/movesets/*.json (docs/DATA_FORMATS.md),
/// and MoveLibrary, every move and moveset of a battle together with the
/// rules that pick a move for an input.
///
/// A moveset maps inputs (combat/move_input.hpp) to moves by id. It may
/// inherit another set: what it does not map, the parent does (a sword keeps
/// the kicks of "unarmed"). A set with "pair" is used instead of the main
/// hand's set when the fighter holds exactly that pair of sets' items, main
/// hand first (decision 2026-10-08: a separate set per pair).
///
/// \code
///   // data/movesets/sword.json; the id is the file stem
///   {"inherit": "unarmed",
///    "moves": {"Heavy": "sword_slash", "Forward+Heavy": "sword_thrust"},
///    "stance": "stance_sword",
///    "block": {"damage_scale": 0.1, "max_level": "Touch",
///              "clips": {"Mid": "block_mid_sword"}}}
///
///   // data/movesets/sword_shield.json
///   {"pair": ["sword", "shield"], "inherit": "sword",
///    "moves": {"Special": "shield_bash"},
///    "block": {"covers": {"Mid": ["Head", "Torso", "UpperArmL", "ForearmL"]}}}
/// \endcode
///
/// Choosing a move for an input: the directions InputRules::getTryOrder()
/// gives, in order; for each, the set and then its parents; within one set
/// the matching entry with the most buttons wins (findMove()).
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "combat/commands.hpp"
#include "combat/events.hpp"
#include "combat/move_input.hpp"
#include "combat/moves.hpp"
#include "core/body.hpp"

namespace fighter::combat {

inline constexpr size_t BlockZoneCount = 3;

/// The block of a moveset. Every field is optional in data; an absent one
/// keeps the value of the parent set, and at the root the general rules
/// (data/reactions.json "block", the clips block_high/mid/low, the zones of
/// decision O.2).
struct BlockDef {
    std::optional<float> DamageScale;          ///< Share of damage that goes through.
    std::optional<ReactionLevel> MaxLevel;     ///< The strongest reaction to a blocked hit.
    /// Multiplies the stamina a blocked hit costs (reactions.json
    /// "stamina_per_strength"): a shield takes hits cheaper.
    std::optional<float> StaminaScale;
    /// Per zone (indexed by BlockZone): the clip that holds this guard.
    std::array<std::optional<std::string>, BlockZoneCount> Clips{};
    /// Per zone: the body parts the guard covers (a shield's Mid can cover
    /// the head too).
    std::array<std::optional<std::vector<BodyPart>>, BlockZoneCount> Covers{};
};

/// One line of a moveset: this input starts this move.
struct MoveSetEntry {
    MoveInput Input;
    std::string MoveId;
};

struct MoveSet {
    std::string Id;                    ///< The file stem: data/movesets/<Id>.json.
    std::string Inherit;               ///< The parent set; empty: none.
    /// For a pair set: the sets of the main-hand and the off-hand items.
    std::vector<std::string> Pair;
    std::vector<MoveSetEntry> Entries;
    BlockDef Block;
    /// The clip of the idle stance; absent: the parent's, at the root the
    /// general "stance" clip.
    std::optional<std::string> Stance;
};

/// The block a fighter has, after the parents and the general rules.
struct BlockRules {
    float DamageScale = 0.2f;
    ReactionLevel MaxLevel = ReactionLevel::Touch;
    float StaminaScale = 1.0f;
    std::array<std::string, BlockZoneCount> Clips{};
    std::array<std::vector<BodyPart>, BlockZoneCount> Covers{};

    const std::string& getClip(BlockZone Zone) const { return Clips[static_cast<size_t>(Zone)]; }
    bool covers(BlockZone Zone, BodyPart Part) const;
};

/// The moveset used without a weapon, and the root of the others.
inline constexpr std::string_view UnarmedMoveSetId = "unarmed";

/// One thing wrong in the data: the file (relative to the data directory,
/// for example "movesets/sword.json") and what is wrong in it, naming the
/// field and the value.
struct DataProblem {
    std::string File;
    std::string Detail;

    /// "File: Detail".
    std::string format() const { return File + ": " + Detail; }
    bool operator==(const DataProblem&) const = default;
};

/// Every move and moveset of a battle, checked against each other.
class MoveLibrary {
public:
    /// Reads data/moves/, data/movesets/ and data/input.json under
    /// \p DataDir and checks every reference (validate()). Throws
    /// std::runtime_error that names the file.
    static MoveLibrary load(const std::filesystem::path& DataDir);

    /// Builds a library from parts (for tests). Throws like load().
    static MoveLibrary build(std::vector<MoveDef> Moves, std::vector<MoveSet> Sets, InputRules Input);

    const std::vector<MoveDef>& getMoves() const { return Moves; }
    const std::vector<MoveSet>& getSets() const { return Sets; }
    const InputRules& getInputRules() const { return Input; }

    /// Builds a library without checking it: for the data check
    /// (combat/data_check.hpp), which reports every problem findProblems()
    /// finds. Looking up through such a library is safe, but it may hold
    /// circular parents; use only to list the problems.
    static MoveLibrary buildUnchecked(std::vector<MoveDef> Moves, std::vector<MoveSet> Sets, InputRules Input);

    /// Every broken reference between the moves and the sets: a missing
    /// "unarmed" set, an input that names no move, a parent or pair member
    /// that is no set, parents in a circle, two sets for one pair. Empty if
    /// the library is sound; build() and load() throw the first one.
    std::vector<DataProblem> findProblems() const;

    const MoveDef* findMove(std::string_view Id) const { return findMoveById(Moves, Id); }
    const MoveSet* findSet(std::string_view Id) const;

    /// The set of a fighter whose main hand holds an item of set \p MainSet
    /// and the other hand one of \p OffSet (empty: nothing, or no set): the
    /// pair set for exactly these two if there is one, otherwise MainSet,
    /// otherwise OffSet if \p OffAlone (an off-hand item that strikes on its
    /// own; a shield in the off hand does not, decision 2026-10-08),
    /// otherwise "unarmed".
    const MoveSet& selectSet(std::string_view MainSet, std::string_view OffSet, bool OffAlone = true) const;

    /// The move started in \p Set with \p Direction held (see the file
    /// comment), or nullptr. An entry matches when all its buttons are in
    /// \p Held and at least one of them is in \p Pressed (the buttons that
    /// start a strike now: newly pressed, or held to repeat it).
    const MoveDef* findMove(const MoveSet& Set, InputDirection Direction, ButtonSet Pressed, ButtonSet Held) const;
    /// The same choice as findMove(), as the matching line of the set (its
    /// input tells which direction matched), or nullptr.
    const MoveSetEntry* findEntry(const MoveSet& Set, InputDirection Direction, ButtonSet Pressed,
                                  ButtonSet Held) const;

    /// Could more buttons pressed with \p Buttons still start another move:
    /// is there a line in \p Set or its parents, for a direction tried for
    /// \p Direction, with every button of \p Buttons and more? Then a press
    /// waits for the rest of the combination (InputRules::ComboWindowSec).
    bool canGrowCombo(const MoveSet& Set, InputDirection Direction, ButtonSet Buttons) const;

    /// The block of \p Set with its parents and the defaults filled in.
    /// \p Defaults are the general rules (reactions.json, the O.2 zones).
    BlockRules getBlock(const MoveSet& Set, const BlockRules& Defaults) const;

    /// The idle stance clip of \p Set: its own, else the nearest parent's,
    /// else \p Default (the general stance clip).
    std::string getStance(const MoveSet& Set, std::string_view Default) const;

private:
    std::vector<MoveDef> Moves;
    std::vector<MoveSet> Sets;
    InputRules Input;
};

/// Parses a moveset from JSON text. Throws std::runtime_error that names the
/// field.
MoveSet parseMoveSet(std::string_view JsonText, std::string Id);

/// Reads every *.json file in \p Dir (in name order); each file is one set
/// named after its stem. Throws std::runtime_error that names the file.
std::vector<MoveSet> loadMoveSets(const std::filesystem::path& Dir);

} // namespace fighter::combat
