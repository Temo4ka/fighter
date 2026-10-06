//===- combat/clip_library.hpp - The clips a battle plays -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ClipLibrary, the clips of data/poses/ that the
/// fighters' state machine plays: the stance, walking, crouching (also walking crouched), the three
/// block zones, the reactions and every clip the moves name. Every clip that
/// poses the legs also has a copy played with the other leg
/// (anim::mirrorClipLegs), for an action that starts with the right foot in
/// front (CombatTuning::LegStep, "stanceAfterStop").
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "anim/clip.hpp"
#include "combat/commands.hpp"
#include "combat/events.hpp"
#include "combat/moves.hpp"

namespace fighter::combat {

/// Clip names the state machine plays besides the moves' own clips.
namespace clips {
inline constexpr std::string_view Stance = "stance";
inline constexpr std::string_view Walk = "walk";
inline constexpr std::string_view Crouch = "crouch";
inline constexpr std::string_view CrouchWalk = "crouch_walk";
inline constexpr std::string_view BlockHigh = "block_high";
inline constexpr std::string_view BlockMid = "block_mid";
inline constexpr std::string_view BlockLow = "block_low";
inline constexpr std::string_view Flinch = "flinch";
inline constexpr std::string_view Stagger = "stagger";
inline constexpr std::string_view Knockback = "knockback";
} // namespace clips

class ClipLibrary {
public:
    /// Reads the clips of the state machine and of \p Moves (Clip and
    /// CloseClip) from \p PosesDir. Throws std::runtime_error that names the
    /// file if one is missing or broken.
    static ClipLibrary load(const std::filesystem::path& PosesDir, std::span<const MoveDef> Moves);

    /// The clip loaded for \p Name. Throws std::out_of_range if it was not
    /// loaded.
    const anim::Clip& get(std::string_view Name) const;
    /// The clip loaded for \p Name, or nullptr (an optional clip without its
    /// file).
    const anim::Clip* find(std::string_view Name) const;
    const anim::Clip& getBlock(BlockZone Zone) const;
    /// The clip of a reaction level, or nullptr if the level has none (None,
    /// Touch, and Knockdown, which the rig plays as a ragdoll).
    const anim::Clip* findReaction(ReactionLevel Level) const;
    /// \p Source played with the other leg: its mirrored copy if it poses
    /// the legs, else \p Source itself (also for a clip of another library).
    const anim::Clip& getMirrored(const anim::Clip& Source) const;
    /// The clip as authored: \p Source, or the one it is the mirrored copy of.
    const anim::Clip& getAuthored(const anim::Clip& Source) const;
    /// Is \p Source a mirrored copy?
    bool isMirrored(const anim::Clip& Source) const;

private:
    void add(const std::filesystem::path& PosesDir, std::string_view Name);

    std::map<std::string, anim::Clip, std::less<>> Clips;
    /// The mirrored copies of the clips that pose the legs, by the name of the authored clip.
    std::map<std::string, anim::Clip, std::less<>> Mirrored;
};

} // namespace fighter::combat
