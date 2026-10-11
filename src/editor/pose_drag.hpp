//===- editor/pose_drag.hpp - Mouse edits of the ghost pose -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the mouse edits of the pose editor without the window:
/// turning a joint by dragging its part, moving a fist (arm IK) and pointing
/// the weapon by its tip, each on the full pose of the ghost
/// (composeGhostPose()), and writing the result into a key of the clip.
///
/// The edits work on the ghost's own parts: with a clip played with its arms
/// swapped, the arm that visibly holds the weapon is moved, and
/// applyGhostEdit() writes its angles to the joints the clip file names
/// (getGhostPart()). With a two-handed weapon (GhostContext::GripHand) every
/// edit that moves the weapon re-solves the gripping arm so that its fist
/// stays on the handle (getGripPoint()). The root is not edited; there is no
/// leg IK, a leg is turned joint by joint.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstddef>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "editor/arm_ik.hpp"
#include "editor/ghost.hpp"
#include "editor/pose_fk.hpp"

namespace fighter::editor {

/// A mouse edit of the ghost.
struct GhostEdit {
    /// The ghost before the edit; its wrist is set (the item's angle when the
    /// clip and the stance leave it out).
    anim::Pose Before;
    anim::Pose After;                       ///< The ghost after the edit.
    std::bitset<BodyPartCount> Joints;      ///< The ghost joints the edit sets.
    bool Wrist = false;                     ///< The edit sets the wrist.
};

/// Can the mouse turn the joint of \p Part: any joint of the rig (torso, head,
/// arms, legs), not the root. A leg part turns about its own joint and takes
/// the parts below it along (thigh: shin and foot).
bool isDraggablePart(const rig::RigDef& Def, BodyPart Part);

/// The distance from \p Point to the surface of \p Part placed at \p Posed,
/// m; negative inside.
float getDistanceToPart(const rig::PartDef& Part, const PosedPart& Posed, Vec2 Point);

/// Turns the joint of the ghost part \p Part by the drag from \p From to
/// \p To about its pivot (dragJointAngle()), from the pose \p Ghost.
GhostEdit rotateGhostJoint(const GhostContext& Context, const anim::Pose& Ghost, BodyPart Part, Vec2 From, Vec2 To,
                           ElbowBend Bend);

/// Moves the fist of the ghost forearm \p Forearm to \p Target (solveArmIk()).
GhostEdit moveGhostFist(const GhostContext& Context, const anim::Pose& Ghost, BodyPart Forearm, Vec2 Target,
                        ElbowBend Bend);

/// Points the weapon of the weapon hand at \p Target (aimWeapon()). Without a
/// weapon nothing changes.
GhostEdit aimGhostWeapon(const GhostContext& Context, const anim::Pose& Ghost, Vec2 Target, ElbowBend Bend);

/// Writes \p Edit into the key \p KeyIndex of \p Edited: every joint it sets,
/// on the joint of the file that moves that ghost part (\p PlayedOtherHand,
/// getGhostPart()). A joint (or the wrist) the clip does not key yet is first
/// added to every key at its angle before the edit, so the other keys look the
/// same.
void applyGhostEdit(const GhostEdit& Edit, anim::Clip& Edited, size_t KeyIndex, bool PlayedOtherHand);

} // namespace fighter::editor
