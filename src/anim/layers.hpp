//===- anim/layers.hpp - Leg and upper-body layers --------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the two layers a fighter's pose is made of: the legs
/// (both thighs, shins and feet) and the upper body (everything else: the
/// pelvis lean, torso, head and arms), like the separate legs and torso of
/// a Metal Slug sprite.
///
/// A clip addresses the parts it poses (its mask, see anim::Clip), so which
/// layer a clip plays on follows from its keys: a clip that poses any leg
/// joint uses the legs (walk, kicks, crouch, low block); one that poses
/// none plays on the upper body only and leaves the legs to whatever they
/// do (punches, the high and middle blocks, the flinch and the stagger).
///
/// Mirroring the legs swaps the roles of the two legs (ThighL <-> ThighR,
/// ShinL <-> ShinR, FootL <-> FootR): a kick authored with the left leg in
/// front plays with the right one. The upper body is not mirrored by it.
///
/// Swapping the arms does the same for the arms (UpperArmL <-> UpperArmR,
/// ForearmL <-> ForearmR): a strike authored with the weapon in the right
/// hand plays with the left one when the weapon is held there.
///
/// The wrists go with the arms. A pose names them by role, not by side:
/// "Weapon" is the wrist of the clip's weapon arm, "WeaponOff" that of the
/// other arm. The weapon arm is the arm the clip strikes with (its strikers),
/// so swapping the arms, which swaps the strikers, moves both wrists to the
/// other forearm with the arm joints, and the pose's fields stay as they are
/// (getWeaponArm()). A clip that strikes with no arm (the stance, blocks,
/// kicks) has the main hand as its weapon arm.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "core/body.hpp"

namespace fighter::anim {

/// Which layer a joint belongs to.
enum class Layer : uint8_t {
    Legs,
    Upper,
};

/// The joints of the leg layer: both thighs, shins and feet.
std::bitset<BodyPartCount> getLegJoints();

/// The layer of the joint whose child is \p Part.
Layer getLayer(BodyPart Part);

/// Does the clip pose any joint of the leg layer?
bool usesLegs(const Clip& Source);

/// The joints of \p Source that \p Joints holds; the others and the wrist
/// (Pose::WeaponAngle) are unset.
Pose selectJoints(const Pose& Source, const std::bitset<BodyPartCount>& Joints);

/// Joins two layers: the leg joints of \p Legs over \p Upper (whose own
/// leg joints are dropped). The wrist is the upper layer's.
Pose joinLayers(const Pose& Upper, const Pose& Legs);

/// The same part on the other leg (ThighL -> ThighR, FootR -> FootL); any
/// other part is returned as it is.
BodyPart getMirroredLegPart(BodyPart Part);

/// \p Source with the two legs swapped: the angles and the mask of each leg
/// joint move to the same joint of the other leg.
Pose mirrorLegs(const Pose& Source);

/// \p Parts with the parts of the two legs swapped (a clip's strikers).
std::bitset<BodyPartCount> mirrorLegParts(const std::bitset<BodyPartCount>& Parts);

/// \p Source played with the other leg: every key and the strikers mirrored
/// (mirrorLegs, mirrorLegParts); timing, stiffness and blends unchanged.
/// The name gets the suffix MirroredSuffix.
Clip mirrorClipLegs(const Clip& Source);

/// Appended to the name of a clip played with the other leg.
inline constexpr std::string_view MirroredSuffix = " (mirrored)";

/// The same part on the other arm (UpperArmL -> UpperArmR, ForearmR ->
/// ForearmL); any other part is returned as it is.
BodyPart getMirroredArmPart(BodyPart Part);

/// Does the clip pose an arm joint or strike with an arm?
bool usesArms(const Clip& Source);

/// \p Source with the two arms swapped: the angles and the mask of each arm
/// joint move to the same joint of the other arm. The wrists (Weapon,
/// WeaponOff) stay: they are named by role (see the file comment).
Pose mirrorArms(const Pose& Source);

/// The forearm whose wrist the key "Weapon" of \p Source sets as it plays:
/// \p MainForearm (the main hand), unless the clip strikes only with the
/// other arm (a forearm or an upper arm of it among its strikers), then the
/// other forearm. The key "WeaponOff" sets the wrist of the other one.
BodyPart getWeaponArm(const Clip& Source, BodyPart MainForearm);

/// \p Source with its two wrists (Weapon and WeaponOff, values and whether
/// they are set) exchanged: a pose of a clip whose weapon arm is the off
/// hand, turned into the main hand's terms.
Pose swapWrists(const Pose& Source);

/// \p Parts with the parts of the two arms swapped (a clip's strikers).
std::bitset<BodyPartCount> mirrorArmParts(const std::bitset<BodyPartCount>& Parts);

/// \p Source played with the other arm: every key and the strikers swapped
/// (mirrorArms, mirrorArmParts); timing, stiffness and blends unchanged. The
/// name gets the suffix OtherHandSuffix.
Clip mirrorClipArms(const Clip& Source);

/// Appended to the name of a clip played with the other arm.
inline constexpr std::string_view OtherHandSuffix = " (other hand)";

} // namespace fighter::anim
