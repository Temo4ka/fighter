#include "rig/rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <ranges>
#include <utility>

#include "debug/draw.hpp"

namespace fighter::rig {
namespace {

constexpr float Pi = std::numbers::pi_v<float>;

/// \name Debug drawing scales
/// @{
constexpr float JointLimitRadius = 0.07f;   ///< m.
constexpr float MotorArcRadius = 0.045f;    ///< m.
constexpr float UprightArcRadius = 0.22f;   ///< m.
constexpr float WalkForceScale = 0.002f;    ///< m per N.
constexpr float VelocityScale = 0.1f;       ///< m per m/s.
constexpr float MinDrawnSpeed = 0.05f;      ///< m/s.
constexpr int CapsuleCapSegments = 6;
/// @}

PartDef mirrorPart(const PartDef& Source, float Facing);
PartDef moveShape(const PartDef& Source, Vec2 Offset);
Vec2 getBoundsCenter(const PartDef& Shape);
Vec2 getBoundsSize(const PartDef& Shape);
physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup);
float wrapAngle(float Angle);
bool isFoot(BodyPart Part);
Vec2 getDirection(float Angle);
void drawShape(debug::Cat Category, const PartDef& Shape, Vec2 Position, float Angle);

} // namespace

Rig::Rig(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup)
    : Control(Def.Control),
      Root(Def.Root),
      Facing(Setup.FacingRight ? 1.0f : -1.0f),
      FighterIndex(Setup.FighterIndex),
      MotorMaxTorque(Setup.MotorMaxTorque * Def.Control.TorqueScale),
      MotorGain(Setup.MotorGain * Def.Control.GainScale) {
    // All parts of one fighter share a negative group: no self-collision.
    const int CollisionGroup = -(static_cast<int>(Setup.FighterIndex) + 1);

    // Every body starts at angle 0 in the reference pose, so reference
    // coordinates relative to a body origin are also its local coordinates.
    PerBodyPart<Vec2> Centers{};
    for (const auto& Source : Def.Parts) {
        const PartDef Mirrored = mirrorPart(Source, Facing);
        const Vec2 Center = getBoundsCenter(Mirrored);
        const auto Index = static_cast<size_t>(Source.Part);
        Centers[Index] = Center;

        PartState& State = Parts[Index];
        State.Shape = moveShape(Mirrored, -Center);
        State.Size = getBoundsSize(Mirrored);
        State.Handle = PhysWorld.createBody({
            .Position = Setup.Origin + Center,
            .AngularDamping = Control.AngularDamping,
            .Part = physics::PartRef{Setup.FighterIndex, Source.Part},
        });
        PhysWorld.addShape(State.Handle, makeShapeDef(State.Shape, CollisionGroup));
        State.Handle.setMass(Setup.MassKg[Index]);
        TotalMass += State.Handle.getMass();
    }

    for (const auto& Source : Def.Joints) {
        const Vec2 Anchor{Source.Anchor.X * Facing, Source.Anchor.Y};
        JointState& Joint = Joints.emplace_back();
        Joint.Child = Source.Child;
        Joint.Parent = Source.Parent;
        Joint.Strength = Source.Strength;
        // Mirroring flips the direction of rotation.
        Joint.LowerAngle = Facing > 0.0f ? Source.LowerAngle : -Source.UpperAngle;
        Joint.UpperAngle = Facing > 0.0f ? Source.UpperAngle : -Source.LowerAngle;
        Joint.AnchorInParent = Anchor - Centers[static_cast<size_t>(Source.Parent)];
        Joint.ChildFromAnchor = Centers[static_cast<size_t>(Source.Child)] - Anchor;
        Joint.RestDirection = std::atan2(Joint.ChildFromAnchor.Y, Joint.ChildFromAnchor.X);
        Joint.Target = std::clamp(0.0f, Joint.LowerAngle, Joint.UpperAngle);
        Joint.Handle = PhysWorld.createRevoluteJoint({
            .BodyA = getPart(Source.Parent).Handle,
            .BodyB = getPart(Source.Child).Handle,
            .Anchor = Setup.Origin + Anchor,
            .LowerAngle = Joint.LowerAngle,
            .UpperAngle = Joint.UpperAngle,
            .MaxMotorTorque = MotorMaxTorque * Source.Strength,
        });
    }
}

void Rig::setTargetAngles(const PerBodyPart<float>& Angles) {
    TargetAngles = Angles;
    for (auto& Joint : Joints) {
        const float Angle = Angles[static_cast<size_t>(Joint.Child)] * Facing;
        Joint.Target = std::clamp(Angle, Joint.LowerAngle, Joint.UpperAngle);
    }
}

void Rig::setMoveVelocity(float Velocity) { MoveVelocity = Velocity; }

void Rig::setBaseStiffness(float Stiffness) { BaseStiffness = Stiffness; }

void Rig::applyControl(float Dt) {
    HitFactor = std::min(1.0f, HitFactor + Control.StiffnessRecovery * Dt);
    const float Stiffness = getStiffness();

    // PD-style motors: a velocity motor whose speed is proportional to the
    // angle error behaves like a stiff spring with damping, and its torque
    // limit is the "strength" of the joint.
    for (auto& Joint : Joints) {
        float Target = Joint.Target;
        if (isFoot(Joint.Child) && getLowestPoint(Joint.Child) < Control.FootLevelHeight) {
            // Foot levelling: a foot on the floor keeps its sole flat instead
            // of following the clip, otherwise the fighter ends up on tiptoe.
            const float ShinAngle = getPart(Joint.Parent).Handle.getAngle();
            Target = std::clamp(-wrapAngle(ShinAngle), Joint.LowerAngle, Joint.UpperAngle);
        }
        // Stiffness scales both the response speed and the torque limit: a
        // stiff joint snaps to its target, a weak one lags and gives way.
        const float Speed = (Target - Joint.Handle.getAngle()) * MotorGain * Stiffness;
        Joint.Handle.setMotorSpeed(std::clamp(Speed, -Control.MaxJointSpeed, Control.MaxJointSpeed));
        Joint.Handle.setMaxMotorTorque(MotorMaxTorque * Joint.Strength * Stiffness);
    }

    // Upright assist: world-space torques on the pelvis and the torso. Hits
    // weaken them, attacks do not make them stronger.
    const float PelvisTarget = TargetAngles[static_cast<size_t>(Root)] * Facing;
    const float TorsoTarget = PelvisTarget + TargetAngles[static_cast<size_t>(BodyPart::Torso)] * Facing;
    PelvisUprightTorque = getUprightTorque(Root, PelvisTarget) * HitFactor;
    TorsoUprightTorque = getUprightTorque(BodyPart::Torso, TorsoTarget) * HitFactor;
    Parts[static_cast<size_t>(Root)].Handle.applyTorque(PelvisUprightTorque);
    Parts[static_cast<size_t>(BodyPart::Torso)].Handle.applyTorque(TorsoUprightTorque);

    // Height assist: an external force that only ever pushes up.
    LiftForce = 0.0f;
    if (isGrounded()) {
        const physics::Body& Pelvis = Parts[static_cast<size_t>(Root)].Handle;
        const float Sag = Control.StandHeight - Pelvis.getPosition().Y;
        const float Lift = Sag * Control.HeightStiffness - Pelvis.getLinearVelocity().Y * Control.HeightDamping;
        LiftForce = std::clamp(Lift, 0.0f, Control.HeightForceLimit) * HitFactor;
        Parts[static_cast<size_t>(Root)].Handle.applyForceToCenter({0.0f, LiftForce});
    }

    // Walking: the legs play the walk cycle, this force sets the speed. It is
    // spread over the parts by mass, like gravity, so it moves the body
    // without tipping it over when the way is blocked.
    WalkForce = {};
    if (MoveVelocity != 0.0f && isGrounded()) {
        const float Error = MoveVelocity - getCenterOfMassVelocity().X;
        WalkForce.X = std::clamp(Error * Control.WalkForceGain, -Control.WalkForceLimit, Control.WalkForceLimit) *
                      HitFactor;
        for (auto& Part : Parts) {
            Part.Handle.applyForceToCenter(WalkForce * (Part.Handle.getMass() / TotalMass));
        }
    }
}

void Rig::applyHit(float Impulse) {
    HitFactor = std::max(Control.MinStiffness, HitFactor - Impulse * Control.StiffnessPerImpulse);
}

float Rig::getMotorTorqueSum() const {
    float Sum = 0.0f;
    for (const auto& Joint : Joints) Sum += std::abs(Joint.Handle.getMotorTorque());
    return Sum;
}

Vec2 Rig::getCenterOfMass() const {
    Vec2 Weighted;
    for (const auto& Part : Parts) Weighted += Part.Handle.getWorldCenterOfMass() * Part.Handle.getMass();
    return Weighted / TotalMass;
}

Vec2 Rig::getCenterOfMassVelocity() const {
    Vec2 Momentum;
    for (const auto& Part : Parts) Momentum += Part.Handle.getLinearVelocity() * Part.Handle.getMass();
    return Momentum / TotalMass;
}

Vec2 Rig::getFloorPoint() const {
    const Vec2 LeftPos = getPartPosition(BodyPart::FootL);
    const Vec2 RightPos = getPartPosition(BodyPart::FootR);
    const float Sole = std::min(getLowestPoint(BodyPart::FootL), getLowestPoint(BodyPart::FootR));
    return {(LeftPos.X + RightPos.X) * 0.5f, std::max(0.0f, Sole)};
}

bool Rig::isGrounded() const {
    return std::min(getLowestPoint(BodyPart::FootL), getLowestPoint(BodyPart::FootR)) < Control.GroundTolerance;
}

float Rig::getLowestPoint(BodyPart Part) const {
    // The lowest corner of the part's bounds: a foot standing on its toes
    // still touches the floor.
    const PartState& State = getPart(Part);
    const Vec2 Center = State.Handle.getPosition();
    const float Angle = State.Handle.getAngle();
    const Vec2 Half = State.Size * 0.5f;
    float Lowest = Center.Y;
    const std::array<Vec2, 4> Corners = {Vec2{-Half.X, -Half.Y}, Vec2{Half.X, -Half.Y}, Vec2{Half.X, Half.Y},
                                         Vec2{-Half.X, Half.Y}};
    for (const auto& Corner : Corners) Lowest = std::min(Lowest, Center.Y + rotate(Corner, Angle).Y);
    return Lowest;
}

Vec2 Rig::getPartPosition(BodyPart Part) const { return getPart(Part).Handle.getPosition(); }

float Rig::getPartAngle(BodyPart Part) const { return getPart(Part).Handle.getAngle(); }

float Rig::getJointAngle(BodyPart Part) const {
    for (const auto& Joint : Joints) {
        if (Joint.Child == Part) return Joint.Handle.getAngle() * Facing;
    }
    return 0.0f;
}

void Rig::getPartTransforms(std::vector<PartTransform>& Out) const {
    Out.clear();
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const PartState& State = Parts[Index];
        Out.push_back({
            .Part = static_cast<BodyPart>(Index),
            .Position = State.Handle.getPosition(),
            .Angle = State.Handle.getAngle(),
            .Size = State.Size,
        });
    }
}

void Rig::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) {
        debug::ScopedSide Owner(FighterIndex == 0 ? debug::Side::Left : debug::Side::Right);
        drawJointsAndMotors();
        drawTargetPose();
        drawForces();

        for (const auto& Part : Parts) {
            const Vec2 Velocity = Part.Handle.getLinearVelocity();
            if (Velocity.getLength() < MinDrawnSpeed) continue;
            debug::drawArrow(debug::Cat::Velocity, Part.Handle.getWorldCenterOfMass(), Velocity * VelocityScale);
        }

        const Vec2 CenterOfMass = getCenterOfMass();
        const Vec2 Support = getFloorPoint();
        debug::drawCross(debug::Cat::CoM, CenterOfMass);
        debug::drawLine(debug::Cat::CoM, CenterOfMass, {CenterOfMass.X, Support.Y});
        debug::drawPoint(debug::Cat::CoM, Support, 0.05f);
    }
}

float Rig::getUprightTorque(BodyPart Part, float TargetAngle) const {
    const physics::Body& Handle = getPart(Part).Handle;
    const float Error = wrapAngle(TargetAngle - Handle.getAngle());
    const float Torque = Error * Control.UprightStiffness - Handle.getAngularVelocity() * Control.UprightDamping;
    return std::clamp(Torque, -Control.UprightTorqueLimit, Control.UprightTorqueLimit);
}

void Rig::drawTargetPose() const {
    // Forward kinematics of the target angles from the real pelvis position:
    // the "ghost" shows where the motors are pulling the body.
    PerBodyPart<Vec2> Positions{};
    PerBodyPart<float> Angles{};
    const auto RootIndex = static_cast<size_t>(Root);
    Positions[RootIndex] = getPart(Root).Handle.getPosition();
    Angles[RootIndex] = TargetAngles[RootIndex] * Facing;

    for (const auto& Joint : Joints) {
        const auto Parent = static_cast<size_t>(Joint.Parent);
        const auto Child = static_cast<size_t>(Joint.Child);
        Angles[Child] = Angles[Parent] + Joint.Target;
        const Vec2 Anchor = Positions[Parent] + rotate(Joint.AnchorInParent, Angles[Parent]);
        Positions[Child] = Anchor + rotate(Joint.ChildFromAnchor, Angles[Child]);
    }

    for (auto&& [Part, Position, Angle] : std::views::zip(Parts, Positions, Angles)) {
        drawShape(debug::Cat::TargetPose, Part.Shape, Position, Angle);
    }
}

void Rig::drawJointsAndMotors() const {
    for (const auto& Joint : Joints) {
        const Vec2 Anchor = Joint.Handle.getAnchor();
        const float Base = getPart(Joint.Parent).Handle.getAngle() + Joint.RestDirection;
        const float Current = Base + Joint.Handle.getAngle();

        debug::drawArc(debug::Cat::Joints, Anchor, JointLimitRadius, Base + Joint.LowerAngle,
                       Base + Joint.UpperAngle);
        debug::drawLine(debug::Cat::Joints, Anchor, Anchor + getDirection(Current) * JointLimitRadius);
        debug::drawPoint(debug::Cat::Joints, Anchor, 0.015f);

        // Motor: an arc from the limb, its length is the share of the torque limit.
        const float MaxTorque = Joint.Handle.getMaxMotorTorque();
        const float Share = MaxTorque > 0.0f ? Joint.Handle.getMotorTorque() / MaxTorque : 0.0f;
        if (std::abs(Share) > 0.02f) {
            debug::drawArc(debug::Cat::Motors, Anchor, MotorArcRadius, Current, Current + Share * Pi);
        }
    }
}

void Rig::drawForces() const {
    const Vec2 Pelvis = getPart(Root).Handle.getWorldCenterOfMass();
    const Vec2 Torso = getPart(BodyPart::Torso).Handle.getWorldCenterOfMass();
    const float Limit = Control.UprightTorqueLimit;
    const std::array<std::pair<Vec2, float>, 2> Upright = {{
        {Pelvis, PelvisUprightTorque},
        {Torso, TorsoUprightTorque},
    }};
    for (const auto& [Center, Torque] : Upright) {
        if (std::abs(Torque) < 1.0f) continue;
        debug::drawArc(debug::Cat::Forces, Center, UprightArcRadius, Pi * 0.5f, Pi * 0.5f + Torque / Limit * Pi);
        debug::drawText(debug::Cat::Forces, Center + Vec2{UprightArcRadius, 0.0f},
                        std::format("{:.0f} Nm", Torque));
    }
    if (WalkForce.X != 0.0f) {
        debug::drawArrow(debug::Cat::Forces, Pelvis, WalkForce * WalkForceScale);
        debug::drawText(debug::Cat::Forces, Pelvis + WalkForce * WalkForceScale + Vec2{0.0f, -0.08f},
                        std::format("walk {:.0f} N", WalkForce.X));
    }
    if (LiftForce > 1.0f) {
        debug::drawArrow(debug::Cat::Forces, Pelvis, {0.0f, LiftForce * WalkForceScale});
        debug::drawText(debug::Cat::Forces, Pelvis + Vec2{-0.35f, -0.08f},
                        std::format("lift {:.0f} N", LiftForce));
    }
}

namespace {

PartDef mirrorPart(const PartDef& Source, float Facing) {
    PartDef Result = Source;
    Result.Begin.X *= Facing;
    Result.End.X *= Facing;
    Result.Center.X *= Facing;
    return Result;
}

PartDef moveShape(const PartDef& Source, Vec2 Offset) {
    PartDef Result = Source;
    Result.Begin += Offset;
    Result.End += Offset;
    Result.Center += Offset;
    return Result;
}

/// The bounding box of a shape, as {min, max}.
std::pair<Vec2, Vec2> getBounds(const PartDef& Shape) {
    const Vec2 Radius{Shape.Radius, Shape.Radius};
    switch (Shape.Shape) {
        case physics::ShapeKind::Circle:
            return {Shape.Center - Radius, Shape.Center + Radius};
        case physics::ShapeKind::Capsule: {
            const Vec2 Low{std::min(Shape.Begin.X, Shape.End.X), std::min(Shape.Begin.Y, Shape.End.Y)};
            const Vec2 High{std::max(Shape.Begin.X, Shape.End.X), std::max(Shape.Begin.Y, Shape.End.Y)};
            return {Low - Radius, High + Radius};
        }
        case physics::ShapeKind::Box:
            return {Shape.Center - Shape.HalfExtents, Shape.Center + Shape.HalfExtents};
    }
    return {};
}

Vec2 getBoundsCenter(const PartDef& Shape) {
    const auto [Low, High] = getBounds(Shape);
    return (Low + High) * 0.5f;
}

Vec2 getBoundsSize(const PartDef& Shape) {
    const auto [Low, High] = getBounds(Shape);
    return High - Low;
}

physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup) {
    return {
        .Kind = Shape.Shape,
        .Center = Shape.Center,
        .Begin = Shape.Begin,
        .End = Shape.End,
        .HalfExtents = Shape.HalfExtents,
        .Radius = Shape.Radius,
        .Friction = Shape.Friction,
        .CollisionGroup = CollisionGroup,
        .EnableHitEvents = true,
    };
}

float wrapAngle(float Angle) { return std::remainder(Angle, 2.0f * Pi); }

bool isFoot(BodyPart Part) { return Part == BodyPart::FootL || Part == BodyPart::FootR; }

Vec2 getDirection(float Angle) { return {std::cos(Angle), std::sin(Angle)}; }

/// Outline of a shape given in body coordinates, placed at \p Position and
/// rotated by \p Angle.
void drawShape(debug::Cat Category, const PartDef& Shape, Vec2 Position, float Angle) {
    auto ToWorld = [&](Vec2 Local) { return Position + rotate(Local, Angle); };
    switch (Shape.Shape) {
        case physics::ShapeKind::Circle:
            debug::drawCircle(Category, ToWorld(Shape.Center), Shape.Radius);
            break;
        case physics::ShapeKind::Capsule: {
            const Vec2 Begin = ToWorld(Shape.Begin);
            const Vec2 End = ToWorld(Shape.End);
            const Vec2 Side = perp((End - Begin).getNormalized()) * Shape.Radius;
            std::array<Vec2, 2 * (CapsuleCapSegments + 1)> Outline;
            for (int Index = 0; Index <= CapsuleCapSegments; ++Index) {
                const float Turn = Pi * static_cast<float>(Index) / static_cast<float>(CapsuleCapSegments);
                Outline[static_cast<size_t>(Index)] = End + rotate(-Side, Turn);
                Outline[static_cast<size_t>(Index + CapsuleCapSegments + 1)] = Begin + rotate(Side, Turn);
            }
            debug::drawPoly(Category, Outline);
            break;
        }
        case physics::ShapeKind::Box: {
            const Vec2 Half = Shape.HalfExtents;
            const std::array<Vec2, 4> Corners = {
                ToWorld(Shape.Center + Vec2{-Half.X, -Half.Y}),
                ToWorld(Shape.Center + Vec2{Half.X, -Half.Y}),
                ToWorld(Shape.Center + Vec2{Half.X, Half.Y}),
                ToWorld(Shape.Center + Vec2{-Half.X, Half.Y}),
            };
            debug::drawPoly(Category, Corners);
            break;
        }
    }
}

} // namespace

} // namespace fighter::rig
