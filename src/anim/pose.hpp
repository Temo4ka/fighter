//===- anim/pose.hpp - Target poses -----------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines anim::Pose, a set of target joint angles that the rig's
/// motors drive the body towards, and the layering of one pose over another.
///
/// Every body part except the root has exactly one joint to its parent, so a
/// joint is named by its child part: Pose::Angles[ShinL] is the knee. The
/// root entry (Pelvis) is the target lean of the pelvis in the world.
///
/// Angles are in radians for a fighter facing right; the rig mirrors them for
/// a fighter facing left. Positive is counter-clockwise: for a fighter facing
/// right, raising an arm forward or bending the elbow is positive, bending the
/// knee is negative.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstddef>

#include "core/body.hpp"

namespace fighter::anim {

struct Pose {
    PerBodyPart<float> Angles{};      ///< rad, see the file comment.
    std::bitset<BodyPartCount> Mask;  ///< Joints this pose sets; others come from the layer below.

    bool hasJoint(BodyPart Part) const { return Mask.test(static_cast<size_t>(Part)); }
    float getAngle(BodyPart Part) const { return Angles[static_cast<size_t>(Part)]; }
    void setAngle(BodyPart Part, float Angle) {
        Angles[static_cast<size_t>(Part)] = Angle;
        Mask.set(static_cast<size_t>(Part));
    }
};

/// Overwrites the joints of \p Base that \p Top sets. Used to play an attack
/// (arms or a leg) over walking or the stance.
inline void layerPose(Pose& Base, const Pose& Top) {
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Top.Mask.test(Index)) continue;
        Base.Angles[Index] = Top.Angles[Index];
        Base.Mask.set(Index);
    }
}

} // namespace fighter::anim
