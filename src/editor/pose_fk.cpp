#include "editor/pose_fk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace fighter::editor {
namespace {

/// The segment of a blade is never shorter than this, m (as in the rig).
constexpr float MinBladeSegment = 0.02f;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

float getLowestPoint(const rig::PartDef& Part, Vec2 Position, float Angle);
void addItems(const rig::RigDef& Def, const anim::Pose& Pose, std::span<const rig::HeldItem> Held, PosedBody& Body);

} // namespace

PosedBody poseBody(const rig::RigDef& Def, const anim::Pose& Pose, std::span<const rig::HeldItem> Held) {
    PosedBody Body;
    Body.Parts[static_cast<size_t>(Def.Root)] = {.Position = {}, .Angle = Pose.getAngle(Def.Root)};
    for (const auto& Joint : Def.Joints) {
        const PosedPart& Parent = Body.get(Joint.Parent);
        const float JointAngle = std::clamp(Pose.getAngle(Joint.Child), Joint.LowerAngle, Joint.UpperAngle);
        const float Angle = Parent.Angle + JointAngle;
        const Vec2 ParentOrigin = getPartOrigin(Def.getPart(Joint.Parent));
        const Vec2 ChildOrigin = getPartOrigin(Def.getPart(Joint.Child));
        const Vec2 Anchor = Parent.Position + rotate(Joint.Anchor - ParentOrigin, Parent.Angle);
        Body.Parts[static_cast<size_t>(Joint.Child)] = {.Position = Anchor + rotate(ChildOrigin - Joint.Anchor, Angle),
                                                        .Angle = Angle};
    }

    // Lift the body so that the lowest kinematic part touches the floor.
    float Lowest = std::numeric_limits<float>::max();
    for (const auto& Part : Def.Parts) {
        if (!Def.Kinematic.test(static_cast<size_t>(Part.Part))) continue;
        const PosedPart& Posed = Body.get(Part.Part);
        Lowest = std::min(Lowest, getLowestPoint(Part, Posed.Position, Posed.Angle));
    }
    if (Lowest != std::numeric_limits<float>::max()) {
        for (auto& Posed : Body.Parts) Posed.Position.Y -= Lowest;
    }
    addItems(Def, Pose, Held, Body);
    return Body;
}

Vec2 getPartOrigin(const rig::PartDef& Part) {
    if (Part.Shape == physics::ShapeKind::Capsule) return (Part.Begin + Part.End) * 0.5f;
    return Part.Center;
}

AngleRange getJointRange(const rig::RigDef& Def, BodyPart Part) {
    for (const auto& Joint : Def.Joints) {
        if (Joint.Child == Part) return {.Lower = Joint.LowerAngle, .Upper = Joint.UpperAngle};
    }
    return {.Lower = -MaxLeanRad, .Upper = MaxLeanRad};
}

AngleRange getWristRange(const rig::RigDef& Def) {
    return {.Lower = Def.Weapon.WristLowerAngle, .Upper = Def.Weapon.WristUpperAngle};
}

namespace {

/// World height of the lowest point of \p Part placed at \p Position and
/// turned by \p Angle, m.
float getLowestPoint(const rig::PartDef& Part, Vec2 Position, float Angle) {
    // The shape relative to the origin of its body, as the rig keeps it.
    const Vec2 Origin = getPartOrigin(Part);
    auto toWorld = [&](Vec2 Point) { return rotate(Point - Origin, Angle); };
    switch (Part.Shape) {
        case physics::ShapeKind::Circle: return Position.Y + toWorld(Part.Center).Y - Part.Radius;
        case physics::ShapeKind::Capsule:
            return Position.Y + std::min(toWorld(Part.Begin).Y, toWorld(Part.End).Y) - Part.Radius;
        case physics::ShapeKind::Box: {
            const Vec2 Half = Part.HalfExtents;
            const std::array<Vec2, 4> Corners = {
                Part.Center + Vec2{-Half.X, -Half.Y}, Part.Center + Vec2{Half.X, -Half.Y},
                Part.Center + Vec2{Half.X, Half.Y}, Part.Center + Vec2{-Half.X, Half.Y}};
            float Lowest = std::numeric_limits<float>::max();
            for (const auto& Corner : Corners) Lowest = std::min(Lowest, toWorld(Corner).Y);
            return Position.Y + Lowest;
        }
    }
    return Position.Y;
}

void addItems(const rig::RigDef& Def, const anim::Pose& Pose, std::span<const rig::HeldItem> Held, PosedBody& Body) {
    for (const auto& Item : Held) {
        const rig::PartDef& Holder = Def.getPart(Item.Part);
        if (Holder.Shape != physics::ShapeKind::Capsule) continue;
        const PosedPart& Forearm = Body.get(Item.Part);
        const Vec2 Axis = (Holder.End - Holder.Begin).getNormalized();
        if (Item.WeaponReachM > 0.0f) {
            const float Radius = Item.WeaponWidthM.value_or(Def.Weapon.Width) * 0.5f;
            const float Segment = std::max(Holder.Radius + Item.WeaponReachM - Radius, MinBladeSegment);
            const float Default =
                Item.WeaponAngleDeg ? *Item.WeaponAngleDeg * RadiansPerDegree : Def.Weapon.Angle;
            const float Wrist = std::clamp(Pose.HasWeapon ? Pose.WeaponAngle : Default, Def.Weapon.WristLowerAngle,
                                           Def.Weapon.WristUpperAngle);
            const Vec2 Fist = Forearm.Position + rotate(Holder.End - getPartOrigin(Holder), Forearm.Angle);
            Body.Weapons.push_back({.Holder = Item.Part,
                                    .Fist = Fist,
                                    .Tip = Fist + rotate(Axis * Segment, Forearm.Angle + Wrist),
                                    .Radius = Radius,
                                    .WristAngle = Wrist,
                                    .ReachM = Item.WeaponReachM});
        }
        if (Item.ShieldLengthM > 0.0f && Item.ShieldWidthM > 0.0f) {
            Body.Shields.push_back({.Holder = Item.Part,
                                    .Center = Forearm.Position,
                                    .HalfExtents = {Item.ShieldLengthM * 0.5f, Item.ShieldWidthM * 0.5f},
                                    .Angle = Forearm.Angle + std::atan2(Axis.Y, Axis.X) +
                                             Item.ShieldAngleDeg * RadiansPerDegree});
        }
    }
}

} // namespace

} // namespace fighter::editor
