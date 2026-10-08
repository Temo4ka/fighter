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
/// A pose may also set the wrists of the held weapons: the angle of a weapon
/// to the forearm that holds it, with the same sign rule (positive turns the
/// weapon counter-clockwise for a fighter facing right; 0 continues the
/// forearm). The clip key "Weapon" (WeaponKey) is the wrist of the clip's
/// weapon arm, "WeaponOff" (WeaponOffKey) that of the other arm (see
/// getWeaponArm() in anim/layers.hpp for which forearm that is). They belong
/// to no body part, and the upper-body layer carries them.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <string_view>

#include "core/body.hpp"

namespace fighter::anim {

/// The clip pose key of the weapon arm's wrist (Pose::WeaponAngle).
inline constexpr std::string_view WeaponKey = "Weapon";
/// The clip pose key of the other arm's wrist (Pose::WeaponOffAngle).
inline constexpr std::string_view WeaponOffKey = "WeaponOff";

struct Pose {
    PerBodyPart<float> Angles{};      ///< rad, see the file comment.
    std::bitset<BodyPartCount> Mask;  ///< Joints this pose sets; others come from the layer below.
    /// The wrist: the held weapon's angle to its forearm, rad (see the file
    /// comment); meaningful only if HasWeapon, else it comes from below.
    float WeaponAngle = 0.0f;
    bool HasWeapon = false;
    /// The wrist of the other arm (the key "WeaponOff"), as WeaponAngle;
    /// meaningful only if HasWeaponOff.
    float WeaponOffAngle = 0.0f;
    bool HasWeaponOff = false;

    bool hasJoint(BodyPart Part) const { return Mask.test(static_cast<size_t>(Part)); }
    float getAngle(BodyPart Part) const { return Angles[static_cast<size_t>(Part)]; }
    void setAngle(BodyPart Part, float Angle) {
        Angles[static_cast<size_t>(Part)] = Angle;
        Mask.set(static_cast<size_t>(Part));
    }
    void setWeaponAngle(float Angle) {
        WeaponAngle = Angle;
        HasWeapon = true;
    }
    void setWeaponOffAngle(float Angle) {
        WeaponOffAngle = Angle;
        HasWeaponOff = true;
    }
};

/// Overwrites the joints (and the wrists) of \p Base that \p Top sets. Used to play an attack
/// (arms or a leg) over walking or the stance.
inline void layerPose(Pose& Base, const Pose& Top) {
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Top.Mask.test(Index)) continue;
        Base.Angles[Index] = Top.Angles[Index];
        Base.Mask.set(Index);
    }
    if (Top.HasWeapon) Base.setWeaponAngle(Top.WeaponAngle);
    if (Top.HasWeaponOff) Base.setWeaponOffAngle(Top.WeaponOffAngle);
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
    if (To.HasWeapon) {
        Result.setWeaponAngle(From.HasWeapon ? From.WeaponAngle + (To.WeaponAngle - From.WeaponAngle) * Weight
                                             : To.WeaponAngle);
    }
    if (To.HasWeaponOff) {
        Result.setWeaponOffAngle(From.HasWeaponOff
                                     ? From.WeaponOffAngle + (To.WeaponOffAngle - From.WeaponOffAngle) * Weight
                                     : To.WeaponOffAngle);
    }
    return Result;
}

} // namespace fighter::anim
