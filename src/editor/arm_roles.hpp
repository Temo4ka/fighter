//===- editor/arm_roles.hpp - Arms by role, not by side ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the mapping between the arm joints a clip file names and
/// the arms the pose editor shows by role: the "weapon arm" (the one that
/// holds the weapon on the ghost, whose forearm is rig::WeaponMount::Part) and
/// the "other arm".
///
/// A clip authored for the other hand is played with the arms swapped
/// (editor::isPlayedOtherHand()), so the clip's ForearmR moves the ghost's
/// ForearmL. A slider named by role always moves the arm that visibly holds the
/// weapon; this mapping says which joint of the file it edits. The file keeps
/// the clip's own joint names: the mapping is only for the panels.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include "core/body.hpp"

namespace fighter::editor {

enum class ArmRole : uint8_t {
    Weapon,   ///< The arm that holds the weapon on the ghost.
    Other,    ///< The other arm.
};

enum class ArmSegment : uint8_t {
    Upper,
    Forearm,
};

/// The joint of the clip file that moves the \p Segment of the arm with
/// \p Role on the ghost. \p MainHand is the forearm that holds the weapon on
/// the ghost (the rig's weapon mount), \p PlayedOtherHand whether the clip is
/// shown with its arms swapped.
BodyPart getRoleJoint(ArmRole Role, ArmSegment Segment, BodyPart MainHand, bool PlayedOtherHand);

/// The role of the arm joint \p Joint of the clip file, or nullopt for a joint
/// that is not an arm joint; with its segment in \p Segment.
std::optional<ArmRole> getJointRole(BodyPart Joint, BodyPart MainHand, bool PlayedOtherHand,
                                    ArmSegment* Segment = nullptr);

/// The body part of the ghost that the clip joint \p Joint moves: the joint's
/// own part, or the same part of the other arm when the arms are swapped.
BodyPart getGhostPart(BodyPart Joint, bool PlayedOtherHand);

} // namespace fighter::editor
