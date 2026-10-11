//===- editor/pose_fk.hpp - Forward kinematics of a pose --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares poseBody(), forward kinematics for the pose editor: the
/// joint angles of an anim::Pose turned into the placement of every body part
/// of a rig::RigDef (and of the items in the hands), without physics.
///
/// It poses the body the way the rig poses its kinematic target
/// (rig::Rig::computeTargetPose): the root part is turned by its own angle
/// (the lean), every child hangs on its parent's joint anchor and is turned by
/// the parent's angle plus its joint angle, clamped to the joint's limits as
/// the rig clamps a motor target. Then the whole body is lifted so that the
/// lowest point of the kinematic parts (the soles) just touches the floor
/// (Rig::getStandingRoot). The body stands at x = 0, facing right.
///
/// A held weapon is a capsule from the fist (the far end of the holding
/// forearm) outwards, turned from the forearm by the wrist angle (the clip key
/// "Weapon"; the item's own angle, else the rig's, when the pose has none),
/// clamped to the wrist limits. A shield is a plate on the forearm.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "anim/pose.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"

namespace fighter::editor {

/// Where a body part is: the origin of its body is the center of its bounds
/// (getPartOrigin()), turned by Angle.
struct PosedPart {
    Vec2 Position;
    float Angle = 0.0f;   ///< rad, counter-clockwise.
};

/// A weapon in a hand: the segment from the fist to the center of the cap
/// at its tip, with the radius of the blade.
struct PosedWeapon {
    BodyPart Holder = BodyPart::ForearmR;
    Vec2 Fist;
    Vec2 Tip;
    float Radius = 0.0f;
    float WristAngle = 0.0f;   ///< rad, to the forearm, after the clamp.
    float ReachM = 0.0f;       ///< Of the item: the surface of the blade ends this far beyond the fist.
};

/// A shield plate on a forearm, a box about its center.
struct PosedShield {
    BodyPart Holder = BodyPart::ForearmL;
    Vec2 Center;
    Vec2 HalfExtents;
    float Angle = 0.0f;
};

struct PosedBody {
    PerBodyPart<PosedPart> Parts{};
    std::vector<PosedWeapon> Weapons;
    std::vector<PosedShield> Shields;

    const PosedPart& get(BodyPart Part) const { return Parts[static_cast<size_t>(Part)]; }
};

/// A range of angles, rad.
struct AngleRange {
    float Lower = 0.0f;
    float Upper = 0.0f;
};

/// The range of the root part (the lean of the body) either way, rad: the
/// root has no joint limits.
inline constexpr float MaxLeanRad = 0.8f;

/// Poses \p Def at the angles of \p Pose (a joint the pose does not set is at
/// 0, the reference pose). \p Held are the items in the hands, shaped as the
/// rig shapes them (combat::getHeldItems()); an item whose part is not a
/// capsule is left out.
PosedBody poseBody(const rig::RigDef& Def, const anim::Pose& Pose, std::span<const rig::HeldItem> Held = {});

/// The origin of the body of \p Part: the center of its bounds in the
/// reference pose. The shape of the part is placed relative to it
/// (shape point - origin).
Vec2 getPartOrigin(const rig::PartDef& Part);

/// The angles the joint of \p Part can take, rad, for a fighter facing right:
/// its limits; for the root +-MaxLeanRad.
AngleRange getJointRange(const rig::RigDef& Def, BodyPart Part);

/// The wrist range of the rig's weapon mount, rad.
AngleRange getWristRange(const rig::RigDef& Def);

} // namespace fighter::editor
