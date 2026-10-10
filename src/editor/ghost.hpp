//===- editor/ghost.hpp - The target pose shown by the editor ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares what the pose editor needs to show a clip: the context
/// it is shown in (the rig, the items in the hands, the idle stance of the
/// moveset) and composeGhostPose(), the full pose of the ghost at a time of
/// the clip.
///
/// A clip keys only some joints, so the ghost completes it the way the game
/// does (combat::Fighter::buildTargetPose): the idle stance of the moveset of
/// the held weapon is the base (its first key; the wrist the stance leaves out
/// is the weapon's own, which poseBody() fills in), and the clip, sampled with
/// anim::sampleClip() like the game samples it, is layered over it. A clip
/// authored for the hand that does not hold the weapon (strikers in the other
/// forearm) is shown played with the arms swapped, as the fighter plays it
/// (anim::mirrorClipArms()). The legs of a clip are shown as authored: the
/// fighter may play a kick mirrored, and the walk cycle and the foot placement
/// of combat refine the legs; the ghost is the target, not the final body.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "core/body.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"

namespace fighter::editor {

/// What a clip is shown against.
struct GhostContext {
    rig::RigDef Rig;
    std::vector<rig::HeldItem> Held;     ///< The items in the hands; empty: bare hands.
    /// The forearm that holds the weapon (rig::WeaponMount::Part); nullopt
    /// without a weapon in the main hand.
    std::optional<BodyPart> WeaponHand;
    anim::Clip Stance;                   ///< The idle stance of the held weapon's moveset.
    std::string ItemName;                ///< Of the held item, for the panel; empty: none.
};

/// Reads the rig (data/rigs/humanoid.json), the item \p ItemId of data/items
/// (empty: bare hands) and the stance of its moveset under
/// \p Root / "data". Throws std::runtime_error that names the file or the id.
GhostContext loadGhostContext(const std::filesystem::path& Root, const std::string& ItemId);

/// The clip as the fighter plays it with the weapon in \p WeaponHand: the
/// clip itself, or its copy with the arms swapped when its strikers are the
/// other forearm (combat::Fighter::chooseStrikeWeapon).
anim::Clip getPlayedClip(const anim::Clip& Authored, std::optional<BodyPart> WeaponHand);

/// Is \p Authored played with the arms swapped for the weapon in \p WeaponHand?
bool isPlayedOtherHand(const anim::Clip& Authored, std::optional<BodyPart> WeaponHand);

/// The full target pose of the ghost at \p TimeSec of \p Edited: the first
/// key of \p Stance with the clip over it (see the file comment).
anim::Pose composeGhostPose(const anim::Clip& Stance, const anim::Clip& Edited, float TimeSec,
                            std::optional<BodyPart> WeaponHand);

} // namespace fighter::editor
