//===- editor/pose_ops.hpp - Copy, paste, mirror of poses -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the operations of the pose editor on the pose of a key:
/// copy it and paste it into another key, mirror it left <-> right, and find
/// the neighbours of a key for the onion skin.
///
/// Mirror and the data convention. The clip file names joints as the clip was
/// authored (a weapon clip for the right hand: ForearmR is the arm that
/// strikes); the game plays it with the arms swapped when the weapon is in the
/// other hand (editor::isPlayedOtherHand()). Mirroring a key is a swap of the
/// joints in the file: ForearmL <-> ForearmR, UpperArmL <-> UpperArmR and the
/// same for the legs, with the angles unchanged (the rig is symmetrical and
/// the angles are relative to the parent; this is what anim::mirrorArms() and
/// anim::mirrorLegs() do). The wrist "Weapon" is relative to the held forearm,
/// whichever it is, so it stays. The strikers belong to the clip, not to a key:
/// mirrorKey() leaves them, mirrorClip() swaps them with the arms of every key
/// (the clip then is the one the game plays for the other hand).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <vector>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "rig/rig_def.hpp"

namespace fighter::editor {

/// The pose of the key \p Index (the joints it sets and the wrist); an empty
/// pose for an index out of range.
anim::Pose copyKeyPose(const anim::Clip& Source, size_t Index);

/// Writes \p Copied over the pose of the key \p Index: the joints that both
/// the copy and the clip set (each kept within the rig's limits), and the
/// wrist if both have it. The time of the key and the joints the clip does not
/// key stay as they are, so the clip stays valid. The number of values
/// written (0 for an index out of range).
size_t pasteKeyPose(anim::Clip& Edited, size_t Index, const anim::Pose& Copied, const rig::RigDef& Def);

/// Swaps the angles of the left and right arm joints and leg joints of the
/// key \p Index. A pair is swapped only when the clip keys both of its
/// joints (the keys of a clip set the same joints, so a swap that moved a
/// joint into a key that has none would leave the clip invalid). The number
/// of pairs left alone for that reason (0 for an index out of range).
size_t mirrorKey(anim::Clip& Edited, size_t Index);

/// Swaps the arms and the legs in every key and the strikers: the clip as the
/// game plays it for the other hand (and mirrored legs). Joints are swapped
/// with their keys, so any clip can be mirrored.
void mirrorClip(anim::Clip& Edited);

/// The keys shown faintly around the key \p Selected.
struct OnionKeys {
    std::vector<size_t> Previous;   ///< Nearest first.
    std::vector<size_t> Next;       ///< Nearest first.
};

/// Up to \p Count keys before and after the key \p Selected. A looping clip
/// goes round (the key after the last is the first), without repeating a key
/// or showing \p Selected itself.
OnionKeys findOnionKeys(const anim::Clip& Source, size_t Selected, size_t Count);

} // namespace fighter::editor
