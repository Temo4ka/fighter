#include "rig/rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
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
constexpr float VelocityScale = 0.1f;       ///< m per m/s.
constexpr float ControllerScale = 0.3f;     ///< m per m/s, the pelvis controller arrows.
constexpr float MinDrawnSpeed = 0.05f;      ///< m/s.
constexpr int CapsuleCapSegments = 6;
/// @}

/// \name Collision categories of body parts (the arena keeps bit 0)
/// A knocked-down fighter ignores the posed parts of the other fighter: a
/// kick still moving through the falling body would fling it, as nothing
/// stops a kinematic leg.
/// @{
constexpr uint64_t PosedPartBit = uint64_t{1} << 1;
constexpr uint64_t PhysicalPartBit = uint64_t{1} << 2;
constexpr uint64_t CollideWithAll = ~uint64_t{0};
/// @}

PartDef mirrorPart(const PartDef& Source, float Facing);
PartDef moveShape(const PartDef& Source, Vec2 Offset);
Vec2 getBoundsCenter(const PartDef& Shape);
Vec2 getBoundsSize(const PartDef& Shape);
physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup, uint64_t Category);
float getLowestPoint(const PartDef& Shape, Vec2 Position, float Angle);
float wrapAngle(float Angle);
float smoothStep(float T);
Vec2 getDirection(float Angle);
void drawShape(debug::Cat Category, const PartDef& Shape, Vec2 Position, float Angle);

} // namespace

std::string_view getPostureName(Posture State) {
    switch (State) {
        case Posture::Standing: return "standing";
        case Posture::KnockedDown: return "knocked down";
        case Posture::GettingUp: return "getting up";
    }
    return "?";
}

Rig::Rig(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup)
    : Physics(&PhysWorld),
      Control(Def.Control),
      Root(Def.Root),
      Facing(Setup.FacingRight ? 1.0f : -1.0f),
      FighterIndex(Setup.FighterIndex),
      MotorMaxTorque(Setup.MotorMaxTorque * Def.Control.TorqueScale),
      MotorGain(Setup.MotorGain * Def.Control.GainScale),
      MoveSpeedScale(Setup.MoveSpeedScale) {
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
        State.Kinematic = Def.Kinematic.test(Index);
        State.Handle = PhysWorld.createBody({
            .Position = Setup.Origin + Center,
            .AngularDamping = Control.AngularDamping,
            .Part = physics::PartRef{Setup.FighterIndex, Source.Part},
        });
        PhysWorld.addShape(State.Handle,
                           makeShapeDef(State.Shape, CollisionGroup, State.Kinematic ? PosedPartBit : PhysicalPartBit));
        // The mass is set while the body is dynamic; the densities stay when
        // it becomes kinematic.
        State.Handle.setMass(Setup.MassKg[Index]);
        State.Mass = State.Handle.getMass();
        TotalMass += State.Mass;
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

    for (auto& Part : Parts) {
        if (Part.Kinematic) PhysWorld.setBodyType(Part.Handle, physics::BodyType::Kinematic);
    }
    // A posed limb strikes with everything between the touching part and
    // the root: a foot kicks with the whole leg.
    PerBodyPart<float> LimbMass{};
    for (const auto& Joint : Joints) {
        const PartState& Child = getPart(Joint.Child);
        if (!Child.Kinematic) continue;
        const float Above = Joint.Parent == Root ? 0.0f : LimbMass[static_cast<size_t>(Joint.Parent)];
        float& Limb = LimbMass[static_cast<size_t>(Joint.Child)];
        Limb = Child.Mass + Above;
        PhysWorld.setStrikeMass(Child.Handle, Limb);
    }
    Controller = PelvisController(getPart(Root).Handle.getPosition().X,
                                  {.WalkAcceleration = Control.WalkAcceleration,
                                   .KnockbackDecay = Control.KnockbackDecay});
}

void Rig::setTargetAngles(const PerBodyPart<float>& Angles) {
    TargetAngles = Angles;
    for (auto& Joint : Joints) {
        const float Angle = Angles[static_cast<size_t>(Joint.Child)] * Facing;
        Joint.Target = std::clamp(Angle, Joint.LowerAngle, Joint.UpperAngle);
    }
}

void Rig::setMoveVelocity(float Velocity) { Controller.setTargetVelocity(Velocity); }

void Rig::setBaseStiffness(float Stiffness) { BaseStiffness = Stiffness; }

void Rig::snapToTargets() {
    Controller.reset(Controller.getX());
    const PerBodyPart<Placement> Pose = computeTargetPose(getStandingRoot());
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        Part.Handle.setTransform(Target.Position, Target.Angle);
        Part.Handle.setLinearVelocity({});
        Part.Handle.setAngularVelocity(0.0f);
    }
}

void Rig::planMotion(float Dt) {
    // A ragdoll on the floor goes where physics takes it.
    if (CurrentPosture == Posture::KnockedDown) return;
    Controller.plan(Dt);
}

void Rig::applyControl(float Dt) {
    HitFactor = std::min(1.0f, HitFactor + Control.StiffnessRecovery * Dt);

    PostureSec += Dt;
    if (CurrentPosture == Posture::KnockedDown && PostureSec >= Control.KnockdownSec) {
        startGettingUp();
    } else if (CurrentPosture == Posture::GettingUp && PostureSec >= Control.GetUpSec) {
        CurrentPosture = Posture::Standing;
        PostureSec = 0.0f;
    }

    if (CurrentPosture != Posture::KnockedDown) {
        Controller.commit(Dt);
        moveKinematicParts(Dt);
    }
    driveMotors();
}

void Rig::applyHit(float Impulse, float Direction) {
    HitFactor = std::max(Control.MinStiffness, HitFactor - Impulse * Control.StiffnessPerImpulse);
    if (CurrentPosture == Posture::KnockedDown) return;

    // The whole fighter takes the impulse: heavier fighters (CON, armor)
    // are pushed back less.
    const float Speed = Impulse * Control.KnockbackScale / TotalMass;
    if (CurrentPosture == Posture::Standing && Speed >= Control.KnockdownSpeed) {
        knockDown(Speed * Direction);
        return;
    }
    Controller.addKnockback(Speed * Direction);
}

bool Rig::isKinematic(BodyPart Part) const {
    return getPart(Part).Kinematic && CurrentPosture != Posture::KnockedDown;
}

float Rig::getStiffness() const { return BaseStiffness * HitFactor * getPostureStiffness(); }

float Rig::getMotorTorqueSum() const {
    float Sum = 0.0f;
    for (const auto& Joint : Joints) Sum += std::abs(Joint.Handle.getMotorTorque());
    return Sum;
}

Vec2 Rig::getCenterOfMass() const {
    Vec2 Weighted;
    for (const auto& Part : Parts) Weighted += Part.Handle.getWorldCenterOfMass() * Part.Mass;
    return Weighted / TotalMass;
}

Vec2 Rig::getFloorPoint() const {
    const PartState& Left = getPart(BodyPart::FootL);
    const PartState& Right = getPart(BodyPart::FootR);
    const Vec2 LeftPos = Left.Handle.getPosition();
    const Vec2 RightPos = Right.Handle.getPosition();
    const float Sole = std::min(getLowestPoint(Left.Shape, LeftPos, Left.Handle.getAngle()),
                                getLowestPoint(Right.Shape, RightPos, Right.Handle.getAngle()));
    return {(LeftPos.X + RightPos.X) * 0.5f, std::max(0.0f, Sole)};
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
        drawController();

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

PerBodyPart<Rig::Placement> Rig::computeTargetPose(Placement RootPlacement) const {
    PerBodyPart<Placement> Pose{};
    Pose[static_cast<size_t>(Root)] = RootPlacement;
    for (const auto& Joint : Joints) {
        const Placement& Parent = Pose[static_cast<size_t>(Joint.Parent)];
        const float Angle = Parent.Angle + Joint.Target;
        const Vec2 Anchor = Parent.Position + rotate(Joint.AnchorInParent, Parent.Angle);
        Pose[static_cast<size_t>(Joint.Child)] = {.Position = Anchor + rotate(Joint.ChildFromAnchor, Angle),
                                                  .Angle = Angle};
    }
    return Pose;
}

Rig::Placement Rig::getStandingRoot() const {
    // Pose the body with the root on the floor line, then lift it so that
    // the lowest kinematic part (a sole) just touches the floor.
    const Placement OnFloor{.Position = {Controller.getX(), 0.0f},
                            .Angle = TargetAngles[static_cast<size_t>(Root)] * Facing};
    const PerBodyPart<Placement> Pose = computeTargetPose(OnFloor);
    float Lowest = std::numeric_limits<float>::max();
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        if (Part.Kinematic) Lowest = std::min(Lowest, getLowestPoint(Part.Shape, Target.Position, Target.Angle));
    }
    return {.Position = {OnFloor.Position.X, -Lowest}, .Angle = OnFloor.Angle};
}

float Rig::getPostureStiffness() const {
    switch (CurrentPosture) {
        case Posture::Standing:
            return 1.0f;
        case Posture::KnockedDown:
            return Control.KnockdownStiffness;
        case Posture::GettingUp: {
            const float Progress = std::clamp(PostureSec / Control.GetUpSec, 0.0f, 1.0f);
            return Control.KnockdownStiffness + (1.0f - Control.KnockdownStiffness) * Progress;
        }
    }
    return 1.0f;
}

void Rig::moveKinematicParts(float Dt) {
    const PerBodyPart<Placement> Pose = computeTargetPose(getStandingRoot());
    // Getting up blends from where the parts lay to the stance.
    const bool Blending = CurrentPosture == Posture::GettingUp;
    const float Blend = Blending ? smoothStep(std::clamp(PostureSec / Control.GetUpSec, 0.0f, 1.0f)) : 1.0f;

    for (auto&& [Part, Target, From] : std::views::zip(Parts, Pose, GetUpFrom)) {
        if (!Part.Kinematic) continue;
        Placement Goal = Target;
        if (Blending) {
            Goal.Position = lerp(From.Position, Target.Position, Blend);
            Goal.Angle = From.Angle + wrapAngle(Target.Angle - From.Angle) * Blend;
        }
        Part.Handle.moveTo(Goal.Position, Goal.Angle, Dt);
    }
}

void Rig::driveMotors() {
    const float Stiffness = getStiffness();
    // PD-style motors: a velocity motor whose speed is proportional to the
    // angle error behaves like a stiff spring with damping, and its torque
    // limit is the "strength" of the joint. A kinematic child needs none.
    for (auto& Joint : Joints) {
        if (isKinematic(Joint.Child)) {
            Joint.Handle.setMotorSpeed(0.0f);
            Joint.Handle.setMaxMotorTorque(0.0f);
            continue;
        }
        // Stiffness scales both the response speed and the torque limit: a
        // stiff joint snaps to its target, a weak one lags and gives way.
        const float Speed = (Joint.Target - Joint.Handle.getAngle()) * MotorGain * Stiffness;
        Joint.Handle.setMotorSpeed(std::clamp(Speed, -Control.MaxJointSpeed, Control.MaxJointSpeed));
        Joint.Handle.setMaxMotorTorque(MotorMaxTorque * Joint.Strength * Stiffness);
    }
}

void Rig::knockDown(float Velocity) {
    CurrentPosture = Posture::KnockedDown;
    PostureSec = 0.0f;
    // The whole body takes the momentum of the hit: every part moves with
    // the knockback speed, the feet catch on the floor and it topples. The
    // motion the parts had is dropped: a physical part hit by a kinematic
    // leg moves as fast as the leg, which says nothing about the whole body.
    for (auto& Part : Parts) {
        if (Part.Kinematic) Physics->setBodyType(Part.Handle, physics::BodyType::Dynamic);
        Part.Handle.setCollisionMask(CollideWithAll & ~PosedPartBit);
        Part.Handle.setLinearVelocity({Velocity, 0.0f});
        Part.Handle.setAngularVelocity(0.0f);
    }
    Controller.reset(getPartPosition(Root).X);
}

void Rig::startGettingUp() {
    CurrentPosture = Posture::GettingUp;
    PostureSec = 0.0f;
    for (auto&& [Part, From] : std::views::zip(Parts, GetUpFrom)) {
        Part.Handle.setCollisionMask(CollideWithAll);
        if (!Part.Kinematic) continue;
        From = {.Position = Part.Handle.getPosition(), .Angle = Part.Handle.getAngle()};
        Physics->setBodyType(Part.Handle, physics::BodyType::Kinematic);
    }
    Controller.reset(getPartPosition(Root).X);
}

void Rig::drawTargetPose() const {
    // Forward kinematics of the target angles from the real pelvis position:
    // the "ghost" shows where the motors are pulling the physical parts.
    // Standing, the kinematic parts match it exactly.
    const Placement RootPlacement{.Position = getPart(Root).Handle.getPosition(),
                                  .Angle = TargetAngles[static_cast<size_t>(Root)] * Facing};
    const PerBodyPart<Placement> Pose = computeTargetPose(RootPlacement);
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        drawShape(debug::Cat::TargetPose, Part.Shape, Target.Position, Target.Angle);
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

void Rig::drawController() const {
    if (CurrentPosture == Posture::KnockedDown) return;
    // Under the pelvis, on the floor: the controller velocity and, below it,
    // the knockback part of it.
    const Vec2 Base{Controller.getX(), 0.05f};
    debug::drawLine(debug::Cat::Velocity, Base, getPart(Root).Handle.getPosition());
    const float Velocity = Controller.getVelocity();
    if (std::abs(Velocity) >= MinDrawnSpeed) {
        debug::drawArrow(debug::Cat::Velocity, Base, {Velocity * ControllerScale, 0.0f},
                         std::format("v {:+.2f}", Velocity));
    }
    const float Knockback = Controller.getKnockback();
    if (std::abs(Knockback) >= MinDrawnSpeed) {
        const Vec2 Below = Base + Vec2{0.0f, -0.1f};
        debug::drawArrow(debug::Cat::Velocity, Below, {Knockback * ControllerScale, 0.0f},
                         std::format("kb {:+.2f}", Knockback));
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

physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup, uint64_t Category) {
    return {
        .Kind = Shape.Shape,
        .Center = Shape.Center,
        .Begin = Shape.Begin,
        .End = Shape.End,
        .HalfExtents = Shape.HalfExtents,
        .Radius = Shape.Radius,
        .Friction = Shape.Friction,
        .CollisionGroup = CollisionGroup,
        .CollisionCategory = Category,
        .EnableHitEvents = true,
    };
}

/// World height of the lowest point of a shape (in body coordinates) placed
/// at \p Position and turned by \p Angle, m.
float getLowestPoint(const PartDef& Shape, Vec2 Position, float Angle) {
    switch (Shape.Shape) {
        case physics::ShapeKind::Circle:
            return Position.Y + rotate(Shape.Center, Angle).Y - Shape.Radius;
        case physics::ShapeKind::Capsule:
            return Position.Y + std::min(rotate(Shape.Begin, Angle).Y, rotate(Shape.End, Angle).Y) - Shape.Radius;
        case physics::ShapeKind::Box: {
            const Vec2 Half = Shape.HalfExtents;
            const std::array<Vec2, 4> Corners = {Vec2{-Half.X, -Half.Y}, Vec2{Half.X, -Half.Y},
                                                 Vec2{Half.X, Half.Y}, Vec2{-Half.X, Half.Y}};
            float Lowest = std::numeric_limits<float>::max();
            for (const auto& Corner : Corners) Lowest = std::min(Lowest, rotate(Shape.Center + Corner, Angle).Y);
            return Position.Y + Lowest;
        }
    }
    return Position.Y;
}

float wrapAngle(float Angle) { return std::remainder(Angle, 2.0f * Pi); }

/// Eases a 0..1 progress in and out: no jerk at the start and the end.
float smoothStep(float T) { return T * T * (3.0f - 2.0f * T); }

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
