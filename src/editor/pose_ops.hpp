//===- editor/pose_ops.hpp - Copy and paste of poses ------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the operations of the pose editor on the pose of a key:
/// copy it and paste it into another key, and find the neighbours of a key
/// for the onion skin.
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
