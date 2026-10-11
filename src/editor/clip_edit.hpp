//===- editor/clip_edit.hpp - Editing the keys of a clip --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the operations of the pose editor on an anim::Clip: add,
/// delete, duplicate and move a key, change the duration, edit the pelvis
/// track. They keep the clip valid for anim::parseClip() (keys sorted, the
/// first at 0, every key inside the clip, every key setting the same joints),
/// so that whatever the panels do, the clip can be saved and loaded again.
///
/// An operation that cannot be done (delete the first key, no room for a
/// duplicate) changes nothing and says so in its result.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>

#include "anim/clip.hpp"

namespace fighter::editor {

/// The least distance between two keys the editor makes, s.
inline constexpr float MinKeyGapSec = 0.01f;

/// Inserts a key at \p TimeSec whose pose is the clip's own pose at that
/// time (so the motion does not change). The index of the new key, or
/// nullopt if \p TimeSec is outside (0, duration] or a key is closer than
/// MinKeyGapSec.
std::optional<size_t> addKey(anim::Clip& Edited, float TimeSec);

/// Removes the key \p Index. False for the first key (the clip starts at 0)
/// and for the only key left.
bool deleteKey(anim::Clip& Edited, size_t Index);

/// Inserts a copy of the key \p Index after it, halfway to the next key (or
/// 0.1 s after the last one, but not past the duration). The index of the
/// copy, or nullopt if there is no room (MinKeyGapSec).
std::optional<size_t> duplicateKey(anim::Clip& Edited, size_t Index);

/// Moves the key \p Index to \p TimeSec, kept between its neighbours (the
/// keys do not swap) and inside the clip. The time it got. The first key
/// stays at 0.
float moveKey(anim::Clip& Edited, size_t Index, float TimeSec);

/// Sets the duration, kept at least as long as the last key, the end of the
/// active phase and the last pelvis key, and positive. The duration it got.
float setDuration(anim::Clip& Edited, float DurationSec);

/// Sets the striking phase, kept inside [0, duration] with begin <= end.
void setActive(anim::Clip& Edited, float BeginSec, float EndSec);

/// Does the clip key the joint of \p Part (all its keys set the same joints)?
bool isJointKeyed(const anim::Clip& Edited, BodyPart Part);

/// Adds the joint of \p Part to every key of the clip, at \p Angle (rad), or
/// removes it from every key. Nothing changes if the clip already is that way.
void setJointKeyed(anim::Clip& Edited, BodyPart Part, bool Keyed, float Angle);

/// The same for the wrist of the held weapon (the key "Weapon").
void setWristKeyed(anim::Clip& Edited, bool Keyed, float Angle);

/// Inserts a key of the pelvis track at \p TimeSec with the offset the track
/// has there (the track is created with its key at 0 if there is none). The
/// index of the new key, or nullopt as for addKey(), or if the clip loops
/// (a looping clip cannot move the pelvis).
std::optional<size_t> addPelvisKey(anim::Clip& Edited, float TimeSec);

/// Removes the pelvis key \p Index; the key at 0 stays. When only that key is
/// left the track is dropped. False if nothing was removed.
bool deletePelvisKey(anim::Clip& Edited, size_t Index);

/// Sets time and offset of the pelvis key \p Index: the time kept between its
/// neighbours and inside the clip (the first key stays at 0 with offset 0), the
/// offset within +-MaxPelvisOffsetM.
void setPelvisKey(anim::Clip& Edited, size_t Index, float TimeSec, float OffsetX);

} // namespace fighter::editor
