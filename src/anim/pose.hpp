//===- anim/pose.hpp - Target poses -----------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines anim::Pose, a set of target joint angles that the rig's
/// motors drive the body towards, and the layering (layerPose) and blending
/// (blendPoses) of poses.
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

#include <algorithm>
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

/// Blends \p From into \p To: \p T = 0 is \p From, 1 is \p To (clamped to
/// that range). A joint that only one of the poses sets keeps that pose's
/// value, since there is nothing to blend it with; the result sets the joints
/// of both.
inline Pose blendPoses(const Pose& From, const Pose& To, float T) {
    const float Weight = std::clamp(T, 0.0f, 1.0f);
    Pose Result = From;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!To.Mask.test(Index)) continue;
        if (From.Mask.test(Index)) {
            Result.Angles[Index] += (To.Angles[Index] - Result.Angles[Index]) * Weight;
        } else {
            Result.Angles[Index] = To.Angles[Index];
            Result.Mask.set(Index);
        }
    }
    return Result;
}

} // namespace fighter::anim
