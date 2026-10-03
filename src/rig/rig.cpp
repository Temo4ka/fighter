#include "rig/rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
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
constexpr float FootLockMarkSize = 0.06f;   ///< m.
constexpr float WallMarkHeight = 1.8f;      ///< m.
/// @}

/// \name Collision categories of body parts
/// The arena keeps the default category (bit 0). A knocked-down fighter
/// ignores the posed parts of the other fighter: a kick still moving through
/// the falling body would fling it, as nothing stops a kinematic leg. Parts
/// of the rig's "passThrough" list ignore each other (RigDef::PassThrough):
/// their category is PassThroughBit alone, and their mask leaves it out. A
/// limb that lets go of the opponent ("unjam") collides with the arena only.
/// @{
constexpr uint64_t ArenaBit = uint64_t{1} << 0;
constexpr uint64_t PosedPartBit = uint64_t{1} << 1;
constexpr uint64_t PhysicalPartBit = uint64_t{1} << 2;
constexpr uint64_t PassThroughBit = uint64_t{1} << 3;
constexpr uint64_t CollideWithAll = ~uint64_t{0};
/// @}

/// A foot offset smaller than this needs no leg correction, m.
constexpr float MinFootOffset = 1e-4f;
/// Keeps the two-bone solution away from a straight or folded leg, m.
constexpr float LegReachMargin = 1e-4f;

PartDef mirrorPart(const PartDef& Source, float Facing);
PartDef moveShape(const PartDef& Source, Vec2 Offset);
Vec2 getBoundsCenter(const PartDef& Shape);
Vec2 getBoundsSize(const PartDef& Shape);
physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup, uint64_t Category, uint64_t Mask);
float getLowestPoint(const PartDef& Shape, Vec2 Position, float Angle);
ExtentX getShapeExtentX(const PartDef& Shape, Vec2 Position, float Angle);
float wrapAngle(float Angle);
float smoothStep(float T);
Vec2 getDirection(float Angle);
float getHeading(Vec2 Vector);
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
      RequestedFacing(Facing),
      FighterIndex(Setup.FighterIndex),
      MotorMaxTorque(Setup.MotorMaxTorque * Def.Control.TorqueScale),
      MotorGain(Setup.MotorGain * Def.Control.GainScale),
      MoveSpeedScale(Setup.MoveSpeedScale),
      WeaponReach(std::max(0.0f, Setup.WeaponReachM)) {
    PerBodyPart<Vec2> Centers{};
    createParts(PhysWorld, Def, Setup, Centers);
    // A rig whose weapon part is not a capsule cannot hold a weapon.
    if (Def.getPart(Def.Weapon.Part).Shape != physics::ShapeKind::Capsule) WeaponReach = 0.0f;
    createJoints(PhysWorld, Def, Setup, Centers);
    for (auto& Part : Parts) {
        if (Part.Kinematic) PhysWorld.setBodyType(Part.Handle, physics::BodyType::Kinematic);
    }
    setStrikeMasses(PhysWorld);
    findLimbs(Def);
    findLegs();
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
    Controller.reset(Controller.getPositionX());
    releaseFeet();
    const PerBodyPart<Placement> Pose = computeTargetPose(getStandingRoot());
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        Part.Handle.setTransform(Target.Position, Target.Angle);
        Part.Handle.setLinearVelocity({});
        Part.Handle.setAngularVelocity(0.0f);
    }
}

void Rig::setFacing(bool FacingRight) { RequestedFacing = FacingRight ? 1.0f : -1.0f; }

void Rig::setStayDown(bool Stay) {
    StayDown = Stay;
    // A knockout drops the fighter where it is, with no push of its own.
    if (StayDown && CurrentPosture != Posture::KnockedDown) knockDown({}, 0.0f);
}

void Rig::planMotion(float Dt) {
    // A ragdoll on the floor goes where physics takes it.
    if (CurrentPosture == Posture::KnockedDown) return;
    Controller.plan(Dt);
}

void Rig::applyControl(float Dt) {
    HitFactor = std::min(1.0f, HitFactor + Control.StiffnessRecovery * Dt);
    PostureSec += Dt;
    advancePosture();

    // A turn waits until the fighter stands: a body getting up is blended
    // from where it lay.
    if (CurrentPosture == Posture::Standing && isTurnPending()) turnAround();
    if (CurrentPosture != Posture::KnockedDown) {
        Controller.commit(Dt);
        moveKinematicParts(Dt);
    }
    updateJams(Dt);
    driveMotors();
}

void Rig::applyHit(float Impulse, float Direction) { applyHit(Impulse, {Direction, 0.0f}, getCenterOfMass()); }

void Rig::applyHit(float Impulse, Vec2 Direction, Vec2 Point) {
    HitFactor = std::max(Control.MinStiffness, HitFactor - Impulse * Control.StiffnessPerImpulse);
    if (CurrentPosture == Posture::KnockedDown) return;

    // The whole fighter takes the impulse: heavier fighters (CON, armor)
    // are pushed back less.
    const float Speed = Impulse * Control.KnockbackScale / TotalMass;
    const Vec2 Push = Direction.getNormalized() * Speed;
    if (CurrentPosture == Posture::Standing && Speed >= Control.KnockdownSpeed) {
        // The push of a rigid body hit off its center of mass also turns it:
        // angular velocity = (r x J) / I about the center of mass.
        const Vec2 Center = getCenterOfMass();
        float Inertia = 0.0f;
        for (const auto& Part : Parts) {
            Inertia += Part.Mass * (Part.Handle.getWorldCenterOfMass() - Center).getLengthSquared();
        }
        const float Spin =
            Inertia > 0.0f ? Control.KnockdownSpin * cross(Point - Center, Push * TotalMass) / Inertia : 0.0f;
        KnockdownPoint = Point;
        knockDown(Push, Spin);
        return;
    }
    Controller.addKnockback(Push.X);
}

void Rig::addPush(float Distance) {
    if (CurrentPosture == Posture::KnockedDown) return;
    // The knockback decays exponentially: its path is speed / decay.
    Controller.addKnockback(Distance * Control.KnockbackDecay);
}

void Rig::updateWallContact(float MinX, float MaxX, float WallX) {
    if (CurrentPosture == Posture::KnockedDown) {
        // A ragdoll touches the wall with whatever part is closest to it.
        const ExtentX Body = getExtentX();
        WallSide = Body.Min <= -WallX + Control.WallTouchDistance  ? -1
                   : Body.Max >= WallX - Control.WallTouchDistance ? 1
                                                                   : 0;
        return;
    }
    Controller.limit(MinX, MaxX);
    const float Planned = Controller.getPlannedX();
    WallSide = Planned <= MinX + Control.WallTouchDistance   ? -1
               : Planned >= MaxX - Control.WallTouchDistance ? 1
                                                             : 0;
}

bool Rig::isKinematic(BodyPart Part) const {
    return getPart(Part).Kinematic && CurrentPosture != Posture::KnockedDown;
}

bool Rig::isUnjamming(BodyPart Part) const {
    const PartState& State = getPart(Part);
    return State.Unjam && getPart(State.Limb).Freed;
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
    const JointState* Joint = findJoint(Part);
    return Joint ? Joint->Handle.getAngle() * Facing : 0.0f;
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

ExtentX Rig::getExtentX() const {
    ExtentX Result{.Min = std::numeric_limits<float>::max(), .Max = std::numeric_limits<float>::lowest()};
    for (const auto& Part : Parts) {
        const ExtentX Shape = getShapeExtentX(Part.Shape, Part.Handle.getPosition(), Part.Handle.getAngle());
        Result.Min = std::min(Result.Min, Shape.Min);
        Result.Max = std::max(Result.Max, Shape.Max);
    }
    if (WeaponReach > 0.0f) {
        const PartState& Holder = getPart(Weapon.Part);
        for (const auto& End : {Weapon.Grip, Weapon.Tip}) {
            const float X = Holder.Handle.getWorldPoint(End).X;
            Result.Min = std::min(Result.Min, X - Weapon.Radius);
            Result.Max = std::max(Result.Max, X + Weapon.Radius);
        }
    }
    return Result;
}

bool Rig::isFootLocked(BodyPart Foot) const {
    return std::ranges::any_of(Legs, [&](const Leg& Limb) { return Limb.Foot == Foot && Limb.Locked; });
}

void Rig::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) {
        debug::ScopedSide Owner(FighterIndex == 0 ? debug::Side::Left : debug::Side::Right);
        drawJointsAndMotors();
        drawTargetPose();
        drawController();
        drawFeetAndLimbs();
        drawWeapon();

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

        // The push of the last knockdown, while the fighter is down.
        if (CurrentPosture == Posture::KnockedDown && KnockdownVelocity.getLength() >= MinDrawnSpeed) {
            debug::drawArrow(debug::Cat::Forces, KnockdownPoint, KnockdownVelocity * ControllerScale,
                             std::format("down {:.2f} m/s, spin {:+.1f}/s", KnockdownVelocity.getLength(),
                                         KnockdownSpinRate));
        }
        fillPanel();
    }
}

void Rig::createParts(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup,
                      PerBodyPart<Vec2>& Centers) {
    // All parts of one fighter share a negative group: no self-collision.
    const int CollisionGroup = -(static_cast<int>(Setup.FighterIndex) + 1);

    // Every body starts at angle 0 in the reference pose, so reference
    // coordinates relative to a body origin are also its local coordinates.
    for (const auto& Source : Def.Parts) {
        const PartDef Mirrored = mirrorPart(Source, Facing);
        const Vec2 Center = getBoundsCenter(Mirrored);
        const auto Index = static_cast<size_t>(Source.Part);
        Centers[Index] = Center;

        PartState& State = Parts[Index];
        State.Shape = moveShape(Mirrored, -Center);
        State.Size = getBoundsSize(Mirrored);
        State.Kinematic = Def.Kinematic.test(Index);
        State.Unjam = Def.Unjam.test(Index);
        const bool PassThrough = Def.PassThrough.test(Index);
        const uint64_t Category = State.Kinematic ? PosedPartBit : PassThrough ? PassThroughBit : PhysicalPartBit;
        State.CollisionMask = PassThrough ? CollideWithAll & ~PassThroughBit : CollideWithAll;
        State.Handle = PhysWorld.createBody({
            .Position = Setup.Origin + Center,
            .AngularDamping = Control.AngularDamping,
            .Part = physics::PartRef{Setup.FighterIndex, Source.Part},
        });
        PhysWorld.addShape(State.Handle, makeShapeDef(State.Shape, CollisionGroup, Category, State.CollisionMask));

        // The weapon is a second capsule of the part that holds it: from the
        // fist (the far end of the part) outwards, so that its surface ends
        // WeaponReach beyond the fist's.
        if (WeaponReach > 0.0f && Source.Part == Def.Weapon.Part && Source.Shape == physics::ShapeKind::Capsule) {
            const PartDef& Holder = State.Shape;
            const Vec2 Axis = (Holder.End - Holder.Begin).getNormalized();
            const Vec2 Direction = rotate(Axis, Def.Weapon.Angle * Facing);
            Weapon = {
                .Part = Source.Part,
                .Grip = Holder.End,
                .Tip = Holder.End + Direction * std::max(Holder.Radius + WeaponReach - Def.Weapon.Radius, 0.0f),
                .Radius = Def.Weapon.Radius,
            };
            PartDef Blade = Holder;
            Blade.Begin = Weapon.Grip;
            Blade.End = Weapon.Tip;
            Blade.Radius = Weapon.Radius;
            PhysWorld.addShape(State.Handle, makeShapeDef(Blade, CollisionGroup, Category, State.CollisionMask));
        }

        // The mass is set while the body is dynamic; the densities stay when
        // it becomes kinematic.
        State.Handle.setMass(Setup.MassKg[Index]);
        State.Mass = State.Handle.getMass();
        TotalMass += State.Mass;
    }
}

void Rig::createJoints(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup,
                       const PerBodyPart<Vec2>& Centers) {
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
        Joint.RestDirection = getHeading(Joint.ChildFromAnchor);
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

void Rig::setStrikeMasses(physics::World& PhysWorld) {
    // A posed limb strikes with everything between the touching part and
    // the root: a foot kicks with the whole leg. The root is the whole
    // posed body: a kick into the pelvis meets both legs standing on the
    // floor, not a free block of 10 kg.
    PerBodyPart<float> LimbMass{};
    float PosedMass = getPart(Root).Mass;
    for (const auto& Joint : Joints) {
        const PartState& Child = getPart(Joint.Child);
        if (!Child.Kinematic) continue;
        const float Above = Joint.Parent == Root ? 0.0f : LimbMass[static_cast<size_t>(Joint.Parent)];
        float& Limb = LimbMass[static_cast<size_t>(Joint.Child)];
        Limb = Child.Mass + Above;
        PhysWorld.setStrikeMass(Child.Handle, Limb);
        PosedMass += Child.Mass;
    }
    PhysWorld.setStrikeMass(getPart(Root).Handle, PosedMass);
}

const Rig::JointState* Rig::findJoint(BodyPart Child) const {
    const auto Found = std::ranges::find(Joints, Child, &JointState::Child);
    return Found == Joints.end() ? nullptr : &*Found;
}

void Rig::findLimbs(const RigDef& Def) {
    // A chain of unjam parts lets go as a whole: a forearm stuck in the
    // opponent also holds the upper arm back. Parents come before children,
    // so a parent's limb is known when its child is reached.
    for (const auto& Joint : Joints) {
        PartState& Child = getPart(Joint.Child);
        if (!Child.Unjam) continue;
        Child.Limb = Def.Unjam.test(static_cast<size_t>(Joint.Parent)) ? getPart(Joint.Parent).Limb : Joint.Child;
    }
}

void Rig::findLegs() {
    // A leg is a chain of three posed joints from the root: hip, knee, ankle.
    const auto findChildJoint = [&](BodyPart Parent) -> std::optional<size_t> {
        for (size_t Index = 0; Index < Joints.size(); ++Index) {
            if (Joints[Index].Parent == Parent && getPart(Joints[Index].Child).Kinematic) return Index;
        }
        return std::nullopt;
    };
    for (size_t Hip = 0; Hip < Joints.size(); ++Hip) {
        if (Joints[Hip].Parent != Root || !getPart(Joints[Hip].Child).Kinematic) continue;
        const auto Knee = findChildJoint(Joints[Hip].Child);
        if (!Knee) continue;
        const auto Ankle = findChildJoint(Joints[*Knee].Child);
        if (!Ankle) continue;
        Legs.push_back({.Hip = Hip, .Knee = *Knee, .Ankle = *Ankle, .Foot = Joints[*Ankle].Child});
    }
}

PerBodyPart<Rig::Placement> Rig::computeTargetPose(Placement RootPlacement,
                                                    const PerBodyPart<float>& Corrections) const {
    PerBodyPart<Placement> Pose{};
    Pose[static_cast<size_t>(Root)] = RootPlacement;
    for (const auto& Joint : Joints) {
        const Placement& Parent = Pose[static_cast<size_t>(Joint.Parent)];
        const float Angle = Parent.Angle + Joint.Target + Corrections[static_cast<size_t>(Joint.Child)];
        const Vec2 Anchor = Parent.Position + rotate(Joint.AnchorInParent, Parent.Angle);
        Pose[static_cast<size_t>(Joint.Child)] = {.Position = Anchor + rotate(Joint.ChildFromAnchor, Angle),
                                                  .Angle = Angle};
    }
    return Pose;
}

Rig::Placement Rig::getStandingRoot() const {
    // Pose the body with the root on the floor line, then lift it so that
    // the lowest kinematic part (a sole) just touches the floor. Bent knees
    // (a crouch) leave the feet higher, so the pelvis goes down.
    const Placement OnFloor{.Position = {Controller.getPositionX(), 0.0f},
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
            return StayDown ? Control.KnockoutStiffness : Control.KnockdownStiffness;
        case Posture::GettingUp: {
            const float Progress = std::clamp(PostureSec / Control.GetUpSec, 0.0f, 1.0f);
            return Control.KnockdownStiffness + (1.0f - Control.KnockdownStiffness) * Progress;
        }
    }
    return 1.0f;
}

void Rig::advancePosture() {
    if (CurrentPosture == Posture::KnockedDown && !StayDown && PostureSec >= Control.KnockdownSec) {
        startGettingUp();
    } else if (CurrentPosture == Posture::GettingUp && PostureSec >= Control.GetUpSec) {
        CurrentPosture = Posture::Standing;
        PostureSec = 0.0f;
    }
}

void Rig::moveKinematicParts(float Dt) {
    const Placement RootPlacement = getStandingRoot();
    PerBodyPart<Placement> Pose = computeTargetPose(RootPlacement);
    if (CurrentPosture == Posture::Standing) {
        Pose = computeTargetPose(RootPlacement, plantFeet(Pose, Dt));
    } else {
        releaseFeet();
    }
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

PerBodyPart<float> Rig::plantFeet(const PerBodyPart<Placement>& Pose, float Dt) {
    PerBodyPart<float> Corrections{};
    for (auto& Limb : Legs) {
        const JointState& Ankle = Joints[Limb.Ankle];
        const Placement& Shin = Pose[static_cast<size_t>(Ankle.Parent)];
        const Vec2 ClipAnkle = Shin.Position + rotate(Ankle.AnchorInParent, Shin.Angle);
        const PartState& Foot = getPart(Limb.Foot);
        const Placement& FootPose = Pose[static_cast<size_t>(Limb.Foot)];
        // The clip plants the foot when its sole is on the floor.
        const bool Planted = getLowestPoint(Foot.Shape, FootPose.Position, FootPose.Angle) <= Control.FootPlantHeight;

        if (Planted && !Limb.Locked) {
            Limb.Locked = true;
            Limb.LockX = ClipAnkle.X + Limb.OffsetX;
        } else if (!Planted && Limb.Locked) {
            Limb.Locked = false;   // lifted: it returns to the clip from where it stood
        }
        if (Limb.Locked) {
            // A pull longer than the slip (a knockback, a push) drags the foot.
            Limb.LockX =
                std::clamp(Limb.LockX, ClipAnkle.X - Control.FootLockSlip, ClipAnkle.X + Control.FootLockSlip);
            Limb.OffsetX = Limb.LockX - ClipAnkle.X;
        } else {
            Limb.OffsetX *= std::exp(-Control.FootLockRelease * Dt);
        }
        if (std::abs(Limb.OffsetX) > MinFootOffset) {
            reachAnkle(Limb, Pose, {ClipAnkle.X + Limb.OffsetX, ClipAnkle.Y}, Corrections);
        }
    }
    return Corrections;
}

void Rig::reachAnkle(const Leg& Limb, const PerBodyPart<Placement>& Pose, Vec2 Ankle,
                     PerBodyPart<float>& Corrections) const {
    const JointState& Hip = Joints[Limb.Hip];
    const JointState& Knee = Joints[Limb.Knee];
    const JointState& AnkleJoint = Joints[Limb.Ankle];
    const Placement& Pelvis = Pose[static_cast<size_t>(Hip.Parent)];
    const Placement& Thigh = Pose[static_cast<size_t>(Hip.Child)];
    const Placement& Foot = Pose[static_cast<size_t>(AnkleJoint.Child)];

    // The bones from hinge to hinge in their bodies' frames (reference pose).
    const Vec2 ThighBone = Knee.AnchorInParent + Hip.ChildFromAnchor;
    const Vec2 ShinBone = AnkleJoint.AnchorInParent + Knee.ChildFromAnchor;
    const float ThighLength = ThighBone.getLength();
    const float ShinLength = ShinBone.getLength();
    const Vec2 HipPoint = Pelvis.Position + rotate(Hip.AnchorInParent, Pelvis.Angle);
    const Vec2 ClipKnee = Thigh.Position + rotate(Knee.AnchorInParent, Thigh.Angle);

    // Two-bone IK: the triangle hip - knee - ankle. Of the two knees, keep
    // the one closer to where the clip has it (bent the same way).
    const Vec2 ToAnkle = Ankle - HipPoint;
    const float Distance = std::clamp(ToAnkle.getLength(), std::abs(ThighLength - ShinLength) + LegReachMargin,
                                      ThighLength + ShinLength - LegReachMargin);
    const float Cosine = (ThighLength * ThighLength + Distance * Distance - ShinLength * ShinLength) /
                         (2.0f * ThighLength * Distance);
    const float Spread = std::acos(std::clamp(Cosine, -1.0f, 1.0f));
    const float Toward = getHeading(ToAnkle);
    const Vec2 KneeA = HipPoint + getDirection(Toward + Spread) * ThighLength;
    const Vec2 KneeB = HipPoint + getDirection(Toward - Spread) * ThighLength;
    const Vec2 KneePoint =
        (KneeA - ClipKnee).getLengthSquared() <= (KneeB - ClipKnee).getLengthSquared() ? KneeA : KneeB;

    // Body angles from the bone directions, then joint angles.
    const float ThighAngle = getHeading(KneePoint - HipPoint) - getHeading(ThighBone);
    const float ShinAngle = getHeading(Ankle - KneePoint) - getHeading(ShinBone);
    const auto setCorrection = [&](const JointState& Joint, float Angle) {
        Corrections[static_cast<size_t>(Joint.Child)] =
            std::clamp(wrapAngle(Angle), Joint.LowerAngle, Joint.UpperAngle) - Joint.Target;
    };
    setCorrection(Hip, ThighAngle - Pelvis.Angle);
    setCorrection(Knee, ShinAngle - ThighAngle);
    setCorrection(AnkleJoint, Foot.Angle - ShinAngle);   // the foot keeps its angle to the floor
}

void Rig::releaseFeet() {
    for (auto& Limb : Legs) {
        Limb.Locked = false;
        Limb.OffsetX = 0.0f;
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

void Rig::updateJams(float Dt) {
    // Two motors pushing limbs into each other hold a deadlock forever: each
    // is at its torque limit, and nothing in the pose changes. A limb that
    // touches the opponent and stays far from its target for JamSec lets go:
    // it passes through the opponent until it no longer overlaps it.
    if (CurrentPosture == Posture::KnockedDown) return;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        PartState& Limb = Parts[Index];
        const auto LimbPart = static_cast<BodyPart>(Index);
        if (!Limb.Unjam || Limb.Limb != LimbPart) continue;

        bool Touching = false;
        bool Overlapping = false;
        float Error = 0.0f;
        for (const auto& Joint : Joints) {
            const PartState& Part = getPart(Joint.Child);
            if (!Part.Unjam || Part.Limb != LimbPart) continue;
            if (Limb.Freed) {
                Overlapping = Overlapping || Physics->isOverlappingOtherFighter(Part.Handle);
            } else {
                Touching = Touching || Physics->isTouchingOtherFighter(Part.Handle);
                Error = std::max(Error, std::abs(Joint.Target - Joint.Handle.getAngle()));
            }
        }
        if (Limb.Freed) {
            if (!Overlapping) setLimbFreed(LimbPart, false);
            continue;
        }
        Limb.StuckSec = Touching && Error > Control.JamAngle ? Limb.StuckSec + Dt : 0.0f;
        if (Limb.StuckSec >= Control.JamSec) setLimbFreed(LimbPart, true);
    }
}

void Rig::setLimbFreed(BodyPart Limb, bool Freed) {
    getPart(Limb).StuckSec = 0.0f;
    for (auto& Part : Parts) {
        if (!Part.Unjam || Part.Limb != Limb) continue;
        Part.Freed = Freed;
        refreshCollisionMask(Part);
    }
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} {}", FighterIndex + 1, getBodyPartName(Limb),
                                    Freed ? "stuck in the opponent: lets go" : "is free again"));
    }
}

void Rig::refreshCollisionMask(PartState& Part) const {
    uint64_t Mask = Part.CollisionMask;
    if (Part.Unjam && getPart(Part.Limb).Freed) Mask = ArenaBit;
    if (CurrentPosture == Posture::KnockedDown) Mask &= ~PosedPartBit;
    Part.Handle.setCollisionMask(Mask);
}

void Rig::knockDown(Vec2 Velocity, float Spin) {
    CurrentPosture = Posture::KnockedDown;
    PostureSec = 0.0f;
    releaseFeet();
    // Against the wall the wall takes the push: the body slumps along it.
    if (WallSide != 0 && Velocity.X * static_cast<float>(WallSide) > 0.0f) Velocity.X = 0.0f;
    KnockdownVelocity = Velocity;
    KnockdownSpinRate = Spin;

    for (auto& Part : Parts) {
        if (Part.Kinematic) Physics->setBodyType(Part.Handle, physics::BodyType::Dynamic);
        Part.Freed = false;
        Part.StuckSec = 0.0f;
    }
    // The whole body takes the momentum of the hit as one rigid body: every
    // part moves with the push plus the spin about the center of mass. The
    // motion the parts had is dropped: a physical part hit by a kinematic leg
    // moves as fast as the leg, which says nothing about the whole body.
    const Vec2 Center = getCenterOfMass();
    for (auto& Part : Parts) {
        refreshCollisionMask(Part);
        Part.Handle.setLinearVelocity(Velocity + perp(Part.Handle.getWorldCenterOfMass() - Center) * Spin);
        Part.Handle.setAngularVelocity(Spin);
    }
    Controller.reset(getPartPosition(Root).X);
}

void Rig::startGettingUp() {
    CurrentPosture = Posture::GettingUp;
    PostureSec = 0.0f;
    for (auto&& [Part, From] : std::views::zip(Parts, GetUpFrom)) {
        refreshCollisionMask(Part);
        if (!Part.Kinematic) continue;
        From = {.Position = Part.Handle.getPosition(), .Angle = Part.Handle.getAngle()};
        Physics->setBodyType(Part.Handle, physics::BodyType::Kinematic);
    }
    Controller.reset(getPartPosition(Root).X);
}

void Rig::turnAround() {
    // Mirror the whole body about the vertical line through the pelvis:
    // positions, angles and velocities, then the shapes and the joints in
    // the bodies' frames. Nothing moves relative to anything else, so the
    // solver has nothing to correct.
    const float AxisX = getPart(Root).Handle.getPosition().X;
    for (auto& Part : Parts) {
        const Vec2 Position = Part.Handle.getPosition();
        const Vec2 Velocity = Part.Handle.getLinearVelocity();
        const float Spin = Part.Handle.getAngularVelocity();
        Part.Handle.setTransform({2.0f * AxisX - Position.X, Position.Y}, -Part.Handle.getAngle());
        Part.Handle.setLinearVelocity({-Velocity.X, Velocity.Y});
        Part.Handle.setAngularVelocity(-Spin);
        Physics->mirrorShapes(Part.Handle);
        Part.Shape = mirrorPart(Part.Shape, -1.0f);
    }
    for (auto& Joint : Joints) {
        Joint.Handle = Physics->mirrorJoint(Joint.Handle);
        const float Lower = Joint.LowerAngle;
        Joint.LowerAngle = -Joint.UpperAngle;
        Joint.UpperAngle = -Lower;
        Joint.AnchorInParent.X = -Joint.AnchorInParent.X;
        Joint.ChildFromAnchor.X = -Joint.ChildFromAnchor.X;
        Joint.RestDirection = getHeading(Joint.ChildFromAnchor);
    }
    Weapon.Grip.X = -Weapon.Grip.X;
    Weapon.Tip.X = -Weapon.Tip.X;
    Facing = RequestedFacing;
    setTargetAngles(TargetAngles);
    releaseFeet();

    // The arms now reach to the other side, maybe into the opponent behind:
    // a limb that overlaps it passes through until it is out.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto LimbPart = static_cast<BodyPart>(Index);
        const PartState& Limb = Parts[Index];
        if (!Limb.Unjam || Limb.Limb != LimbPart) continue;
        const bool Overlapping = std::ranges::any_of(Parts, [&](const PartState& Part) {
            return Part.Unjam && Part.Limb == LimbPart && Physics->isOverlappingOtherFighter(Part.Handle);
        });
        if (Overlapping) setLimbFreed(LimbPart, true);
    }
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} turns to face {}", FighterIndex + 1, Facing > 0.0f ? "right" : "left"));
    }
}

void Rig::drawTargetPose() const {
    // Forward kinematics of the target angles from the real pelvis position:
    // the "ghost" shows where the motors are pulling the physical parts.
    // Standing, the kinematic parts match it except where a planted foot
    // bends the leg.
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
    // the knockback part of it (hits and the push-out).
    const Vec2 Base{Controller.getPositionX(), 0.05f};
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

void Rig::drawFeetAndLimbs() const {
    // Planted feet: a cross where the ankle is held, on the floor.
    for (const auto& Limb : Legs) {
        if (!Limb.Locked) continue;
        debug::drawCross(debug::Cat::Contacts, {Limb.LockX, 0.0f}, FootLockMarkSize);
    }
    // Limbs that let go of the opponent.
    for (const auto& Part : Parts) {
        if (!Part.Unjam || !getPart(Part.Limb).Freed) continue;
        drawShape(debug::Cat::Contacts, Part.Shape, Part.Handle.getPosition(), Part.Handle.getAngle());
        debug::drawText(debug::Cat::Contacts, Part.Handle.getPosition(), "free");
    }
    // The wall the fighter touches.
    if (WallSide != 0) {
        const ExtentX Body = getExtentX();
        const float X = WallSide < 0 ? Body.Min : Body.Max;
        debug::drawLine(debug::Cat::Contacts, {X, 0.0f}, {X, WallMarkHeight});
        debug::drawText(debug::Cat::Contacts, {X, WallMarkHeight}, isAgainstWall() ? "back to wall" : "wall");
    }
}

void Rig::drawWeapon() const {
    if (WeaponReach <= 0.0f) return;
    // The physics draw shows the capsule as a hurtbox; mark it as a weapon.
    const PartState& Holder = getPart(Weapon.Part);
    const Vec2 Tip = Holder.Handle.getWorldPoint(Weapon.Tip);
    debug::drawLine(debug::Cat::Hurtbox, Holder.Handle.getWorldPoint(Weapon.Grip), Tip);
    debug::drawText(debug::Cat::Hurtbox, Tip, std::format("weapon {:.2f} m", WeaponReach));
}

void Rig::fillPanel() const {
    const std::string Name = std::format("P{}", FighterIndex + 1);
    std::string FacingText = isFacingRight() ? "right" : "left";
    if (isTurnPending()) FacingText += std::format(" (turning to {})", RequestedFacing > 0.0f ? "right" : "left");
    debug::setPanel(Name + " facing", FacingText);

    std::string Wall = WallSide == 0 ? "none" : WallSide < 0 ? "left" : "right";
    if (isAgainstWall()) Wall += ", back against it";
    debug::setPanel(Name + " wall", Wall);

    std::string Feet;
    for (const auto& Limb : Legs) {
        if (!Feet.empty()) Feet += ", ";
        Feet += std::format("{} {} {:+.2f}", getBodyPartName(Limb.Foot), Limb.Locked ? "planted" : "free",
                            Limb.OffsetX);
    }
    debug::setPanel(Name + " feet", Feet);

    std::string Limbs;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const PartState& Limb = Parts[Index];
        if (!Limb.Unjam || Limb.Limb != static_cast<BodyPart>(Index)) continue;
        if (!Limbs.empty()) Limbs += ", ";
        Limbs += std::format("{} {}", getBodyPartName(static_cast<BodyPart>(Index)),
                             Limb.Freed ? "free" : Limb.StuckSec > 0.0f ? std::format("stuck {:.2f} s", Limb.StuckSec)
                                                                        : "ok");
    }
    if (StayDown) Limbs += Limbs.empty() ? "stays down" : ", stays down";
    debug::setPanel(Name + " limbs", Limbs);
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

physics::ShapeDef makeShapeDef(const PartDef& Shape, int CollisionGroup, uint64_t Category, uint64_t Mask) {
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
        .CollisionMask = Mask,
        .EnableHitEvents = true,
    };
}

/// The corners of a box shape in body coordinates.
std::array<Vec2, 4> getBoxCorners(const PartDef& Shape) {
    const Vec2 Half = Shape.HalfExtents;
    return {Shape.Center + Vec2{-Half.X, -Half.Y}, Shape.Center + Vec2{Half.X, -Half.Y},
            Shape.Center + Vec2{Half.X, Half.Y}, Shape.Center + Vec2{-Half.X, Half.Y}};
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
            float Lowest = std::numeric_limits<float>::max();
            for (const auto& Corner : getBoxCorners(Shape)) Lowest = std::min(Lowest, rotate(Corner, Angle).Y);
            return Position.Y + Lowest;
        }
    }
    return Position.Y;
}

/// World extent along X of a shape (in body coordinates) placed at
/// \p Position and turned by \p Angle, m.
ExtentX getShapeExtentX(const PartDef& Shape, Vec2 Position, float Angle) {
    const auto spanPoints = [&](std::span<const Vec2> Points, float Radius) {
        ExtentX Result{.Min = std::numeric_limits<float>::max(), .Max = std::numeric_limits<float>::lowest()};
        for (const auto& Point : Points) {
            const float X = Position.X + rotate(Point, Angle).X;
            Result.Min = std::min(Result.Min, X - Radius);
            Result.Max = std::max(Result.Max, X + Radius);
        }
        return Result;
    };
    switch (Shape.Shape) {
        case physics::ShapeKind::Circle:
            return spanPoints(std::span(&Shape.Center, 1), Shape.Radius);
        case physics::ShapeKind::Capsule: {
            const std::array<Vec2, 2> Ends = {Shape.Begin, Shape.End};
            return spanPoints(Ends, Shape.Radius);
        }
        case physics::ShapeKind::Box:
            return spanPoints(getBoxCorners(Shape), 0.0f);
    }
    return {.Min = Position.X, .Max = Position.X};
}

float wrapAngle(float Angle) { return std::remainder(Angle, 2.0f * Pi); }

/// Eases a 0..1 progress in and out: no jerk at the start and the end.
float smoothStep(float T) { return T * T * (3.0f - 2.0f * T); }

Vec2 getDirection(float Angle) { return {std::cos(Angle), std::sin(Angle)}; }

/// Direction of a vector, rad.
float getHeading(Vec2 Vector) { return std::atan2(Vector.Y, Vector.X); }

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
            std::array<Vec2, 4> Corners = getBoxCorners(Shape);
            for (auto& Corner : Corners) Corner = ToWorld(Corner);
            debug::drawPoly(Category, Corners);
            break;
        }
    }
}

} // namespace

} // namespace fighter::rig
