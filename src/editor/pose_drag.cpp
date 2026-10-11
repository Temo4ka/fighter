#include "editor/pose_drag.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "anim/layers.hpp"
#include "editor/arm_roles.hpp"
#include "editor/clip_edit.hpp"

namespace fighter::editor {
namespace {

/// The edit that changes nothing yet: the ghost with its wrist filled in.
GhostEdit startEdit(const GhostContext& Context, const anim::Pose& Ghost);
std::optional<BodyPart> findParent(const rig::RigDef& Def, BodyPart Part);
void setArm(GhostEdit& Edit, const rig::RigDef& Def, BodyPart Forearm, ArmAngles Angles);
/// Puts the gripping fist of a two-handed weapon back on the handle if
/// \p Edit moved the weapon.
void holdGrip(const GhostContext& Context, GhostEdit& Edit, ElbowBend Bend);
bool isSet(const GhostEdit& Edit, BodyPart Part);

} // namespace

bool isDraggablePart(const rig::RigDef& Def, BodyPart Part) {
    return Part != Def.Root && findParent(Def, Part).has_value();
}

float getDistanceToPart(const rig::PartDef& Part, const PosedPart& Posed, Vec2 Point) {
    // The point in the frame of the part file.
    const Vec2 Local = rotate(Point - Posed.Position, -Posed.Angle) + getPartOrigin(Part);
    switch (Part.Shape) {
        case physics::ShapeKind::Circle: return (Local - Part.Center).getLength() - Part.Radius;
        case physics::ShapeKind::Capsule: {
            const Vec2 Spine = Part.End - Part.Begin;
            const float LengthSquared = Spine.getLengthSquared();
            const float T =
                LengthSquared > 0.0f ? std::clamp(dot(Local - Part.Begin, Spine) / LengthSquared, 0.0f, 1.0f) : 0.0f;
            return (Local - (Part.Begin + Spine * T)).getLength() - Part.Radius;
        }
        case physics::ShapeKind::Box: {
            const Vec2 Offset = Local - Part.Center;
            const Vec2 Outside = {std::abs(Offset.X) - Part.HalfExtents.X, std::abs(Offset.Y) - Part.HalfExtents.Y};
            const Vec2 Clamped = {std::max(Outside.X, 0.0f), std::max(Outside.Y, 0.0f)};
            return Clamped.getLength() + std::min(std::max(Outside.X, Outside.Y), 0.0f);
        }
    }
    return 0.0f;
}

GhostEdit rotateGhostJoint(const GhostContext& Context, const anim::Pose& Ghost, BodyPart Part, Vec2 From, Vec2 To,
                           ElbowBend Bend) {
    GhostEdit Edit = startEdit(Context, Ghost);
    if (!isDraggablePart(Context.Rig, Part)) return Edit;
    const PosedBody Body = poseBody(Context.Rig, Edit.Before, Context.Held);
    const float Angle = dragJointAngle(getPosedJointAngle(Context.Rig, Body, Part),
                                       getJointPivot(Context.Rig, Body, Part), From, To,
                                       getJointRange(Context.Rig, Part));
    Edit.After.setAngle(Part, Angle);
    Edit.Joints.set(static_cast<size_t>(Part));
    holdGrip(Context, Edit, Bend);
    return Edit;
}

GhostEdit moveGhostFist(const GhostContext& Context, const anim::Pose& Ghost, BodyPart Forearm, Vec2 Target,
                        ElbowBend Bend) {
    GhostEdit Edit = startEdit(Context, Ghost);
    const PosedBody Body = poseBody(Context.Rig, Edit.Before, Context.Held);
    setArm(Edit, Context.Rig, Forearm, solveArmIk(Context.Rig, Body, Forearm, Target, Bend).Angles);
    holdGrip(Context, Edit, Bend);
    return Edit;
}

GhostEdit aimGhostWeapon(const GhostContext& Context, const anim::Pose& Ghost, Vec2 Target, ElbowBend Bend) {
    GhostEdit Edit = startEdit(Context, Ghost);
    if (!Context.WeaponHand) return Edit;
    const PosedBody Body = poseBody(Context.Rig, Edit.Before, Context.Held);
    const auto Weapon = std::ranges::find(Body.Weapons, *Context.WeaponHand, &PosedWeapon::Holder);
    if (Weapon == Body.Weapons.end()) return Edit;
    const WeaponAim Aim = aimWeapon(Context.Rig, Body, *Weapon, Target);
    Edit.After.setWeaponAngle(Aim.Wrist);
    Edit.Wrist = true;
    setArm(Edit, Context.Rig, Weapon->Holder, Aim.Arm);
    holdGrip(Context, Edit, Bend);
    return Edit;
}

void applyGhostEdit(const GhostEdit& Edit, anim::Clip& Edited, size_t KeyIndex, bool PlayedOtherHand) {
    if (KeyIndex >= Edited.Keys.size()) return;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Edit.Joints.test(Index)) continue;
        const auto GhostPart = static_cast<BodyPart>(Index);
        const BodyPart Joint = getGhostPart(GhostPart, PlayedOtherHand);
        setJointKeyed(Edited, Joint, true, Edit.Before.getAngle(GhostPart));
        Edited.Keys[KeyIndex].Target.setAngle(Joint, Edit.After.getAngle(GhostPart));
    }
    if (Edit.Wrist) {
        setWristKeyed(Edited, true, Edit.Before.WeaponAngle);
        Edited.Keys[KeyIndex].Target.setWeaponAngle(Edit.After.WeaponAngle);
    }
}

namespace {

GhostEdit startEdit(const GhostContext& Context, const anim::Pose& Ghost) {
    GhostEdit Edit;
    Edit.Before = Ghost;
    if (!Edit.Before.HasWeapon && Context.WeaponHand) {
        const PosedBody Body = poseBody(Context.Rig, Ghost, Context.Held);
        const auto Weapon = std::ranges::find(Body.Weapons, *Context.WeaponHand, &PosedWeapon::Holder);
        if (Weapon != Body.Weapons.end()) Edit.Before.setWeaponAngle(Weapon->WristAngle);
    }
    Edit.After = Edit.Before;
    return Edit;
}

std::optional<BodyPart> findParent(const rig::RigDef& Def, BodyPart Part) {
    const auto Found = std::ranges::find(Def.Joints, Part, &rig::JointDef::Child);
    return Found == Def.Joints.end() ? std::nullopt : std::optional(Found->Parent);
}

void setArm(GhostEdit& Edit, const rig::RigDef& Def, BodyPart Forearm, ArmAngles Angles) {
    const std::optional<BodyPart> Upper = findParent(Def, Forearm);
    if (!Upper) return;
    Edit.After.setAngle(*Upper, Angles.Upper);
    Edit.After.setAngle(Forearm, Angles.Forearm);
    Edit.Joints.set(static_cast<size_t>(*Upper));
    Edit.Joints.set(static_cast<size_t>(Forearm));
}

void holdGrip(const GhostContext& Context, GhostEdit& Edit, ElbowBend Bend) {
    if (!Context.GripHand || !Context.WeaponHand) return;
    const BodyPart GripHand = *Context.GripHand;
    const std::optional<BodyPart> GripUpper = findParent(Context.Rig, GripHand);
    const std::optional<BodyPart> WeaponUpper = findParent(Context.Rig, *Context.WeaponHand);
    if (!GripUpper || !WeaponUpper) return;
    // An edit of the gripping arm itself lets go of the handle; one that
    // does not move the weapon leaves the grip as it is.
    if (isSet(Edit, GripHand) || isSet(Edit, *GripUpper)) return;
    const std::optional<BodyPart> Trunk = findParent(Context.Rig, *WeaponUpper);
    const bool MovesWeapon = Edit.Wrist || isSet(Edit, *Context.WeaponHand) || isSet(Edit, *WeaponUpper) ||
                             (Trunk && isSet(Edit, *Trunk));
    if (!MovesWeapon) return;

    const PosedBody Body = poseBody(Context.Rig, Edit.After, Context.Held);
    const auto Weapon = std::ranges::find(Body.Weapons, *Context.WeaponHand, &PosedWeapon::Holder);
    if (Weapon == Body.Weapons.end()) return;
    const ArmSolution Grip = solveArmIk(Context.Rig, Body, GripHand, getGripPoint(*Weapon, Context.GripM), Bend);
    setArm(Edit, Context.Rig, GripHand, Grip.Angles);
}

bool isSet(const GhostEdit& Edit, BodyPart Part) { return Edit.Joints.test(static_cast<size_t>(Part)); }

} // namespace

} // namespace fighter::editor
