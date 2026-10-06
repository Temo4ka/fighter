#include "rig/rig.hpp"

#include <algorithm>
#include <array>
#include <bitset>
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
#include <vector>

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
/// collides with the posed parts of the other fighter like any other body:
/// a posed striker stops at a body it sinks into (stopAtContact), and the
/// standing legs push a lying body out of their way instead of passing
/// through it. Parts of the rig's "passThrough" list ignore each other
/// (RigDef::PassThrough): their category is PassThroughBit alone, and their
/// mask leaves it out.
/// @{
constexpr uint64_t PosedPartBit = uint64_t{1} << 1;
constexpr uint64_t PhysicalPartBit = uint64_t{1} << 2;
constexpr uint64_t PassThroughBit = uint64_t{1} << 3;
constexpr uint64_t CollideWithAll = ~uint64_t{0};
/// @}

/// A physical part this far (rad) from the target pose is marked in the
/// debug draw.
constexpr float MinDrawnPoseError = 0.05f;
/// A foot offset smaller than this needs no leg correction, m.
constexpr float MinFootOffset = 1e-4f;
/// Knockback slower than this lets a standing fighter step its feet back, m/s.
constexpr float MinRestepKnockback = 0.05f;
/// A foot stepping back is there when this close to the stance, m.
constexpr float StepDoneDistance = 0.005f;
/// A planted foot stays this much inside the reach of a straight leg, m.
constexpr float LegReachMargin = 1e-4f;
/// A kept foot is held this much inside the leg's full reach: the pelvis
/// goes down for it rather than straightening the knee completely, m.
constexpr float KeptReachMargin = 0.01f;

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
float getRealizedShare(float Part, float Realized);
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
      YieldAngles(Def.YieldAngles),
      YieldPosed(Def.YieldPosed),
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
                                   .WalkDeceleration = Control.WalkDeceleration,
                                   .KnockbackDecay = Control.KnockbackDecay});
}

void Rig::setTargetAngles(const PerBodyPart<float>& Angles) {
    TargetAngles = Angles;
    for (auto& Joint : Joints) {
        const float Angle = Angles[static_cast<size_t>(Joint.Child)] * Facing;
        Joint.Wish = std::clamp(Angle, Joint.LowerAngle, Joint.UpperAngle);
        Joint.StillWish = Joint.Wish;
    }
    refreshTargets();
}

void Rig::setTravelPose(const PerBodyPart<float>& StillAngles, float Travel) {
    PoseTravel = Travel;
    for (auto& Joint : Joints) {
        const float Angle = StillAngles[static_cast<size_t>(Joint.Child)] * Facing;
        Joint.StillWish = std::clamp(Angle, Joint.LowerAngle, Joint.UpperAngle);
    }
}

void Rig::setMoveVelocity(float Velocity) { Controller.setTargetVelocity(Velocity); }

void Rig::setBaseStiffness(float Stiffness) { BaseStiffness = Stiffness; }

void Rig::snapToTargets() {
    Controller.reset(Controller.getPositionX());
    releaseFeet();
    const PerBodyPart<Placement> Pose = computeTargetPose(getStandingRoot(Controller.getPositionX()));
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        Part.Handle.setTransform(Target.Position, Target.Angle);
        Part.Handle.setLinearVelocity({});
        Part.Handle.setAngularVelocity(0.0f);
    }
    for (auto& Joint : Joints) Joint.PreviousTarget = Joint.Target;
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
    // The knockback the plan moves the pelvis with (it decays in plan()).
    PlannedKnockback = Controller.getKnockback();
    Controller.plan(Dt);
}

void Rig::applyControl(float Dt) {
    StoppedAtContact = false;
    HitFactor = std::min(1.0f, HitFactor + Control.StiffnessRecovery * Dt);
    PostureSec += Dt;
    advancePosture();

    // A turn waits until the fighter stands: a body getting up is blended
    // from where it lay.
    if (CurrentPosture == Posture::Standing && isTurnPending()) turnAround();
    const physics::Body Pelvis = getPart(Root).Handle;
    const Vec2 OldVelocity = Pelvis.getLinearVelocity();
    const float OldSpin = Pelvis.getAngularVelocity();
    if (CurrentPosture != Posture::KnockedDown) {
        followTravel();
        Controller.commit(Dt);
        moveKinematicParts(Dt);
    }
    // A standing fighter carries its upper body along with the pelvis; a
    // ragdoll (and a body getting up from one) moves by physics alone.
    Carrying = CurrentPosture == Posture::Standing;
    if (Carrying) {
        carryPhysicalParts(OldVelocity, OldSpin);
    } else {
        CarriedKnockback = 0.0f;
    }
    const float GravityScale = getCarriedGravityScale();
    for (auto& Part : Parts) {
        if (!Part.Kinematic) Part.Handle.setGravityScale(GravityScale);
    }
    updateJams(Dt);
    driveMotors(Dt);
}

void Rig::applyHit(float Impulse, float Direction) { applyHit(Impulse, {Direction, 0.0f}, getCenterOfMass()); }

void Rig::applyHit(float Impulse, Vec2 Direction, Vec2 Point) {
    const float Speed = Impulse * Control.KnockbackScale / TotalMass;
    applyHit(Impulse, Direction, Point, CurrentPosture == Posture::Standing && Speed >= Control.KnockdownSpeed);
}

void Rig::applyHit(float Impulse, float Direction, bool KnockDown) {
    applyHit(Impulse, {Direction, 0.0f}, getCenterOfMass(), KnockDown);
}

void Rig::applyHit(float Impulse, Vec2 Direction, Vec2 Point, bool KnockDown) {
    HitFactor = std::max(Control.MinStiffness, HitFactor - Impulse * Control.StiffnessPerImpulse);
    if (CurrentPosture == Posture::KnockedDown) return;

    // The whole fighter takes the impulse: heavier fighters (CON, armor)
    // are pushed back less.
    const float Speed = Impulse * Control.KnockbackScale / TotalMass;
    const Vec2 Push = Direction.getNormalized() * Speed;
    if (KnockDown) {
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
    Controller.addPushOut(Distance * Control.KnockbackDecay);
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

std::optional<float> Rig::stopAtContact(const std::bitset<BodyPartCount>& Strikers, float MaxDepth) {
    StoppedAtContact = false;
    if (CurrentPosture == Posture::KnockedDown) return std::nullopt;
    std::vector<physics::Body> Posed;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (Strikers.test(Index) && isKinematic(static_cast<BodyPart>(Index))) Posed.push_back(Parts[Index].Handle);
    }
    if (Posed.empty()) return std::nullopt;
    const physics::Body Pelvis = getPart(Root).Handle;
    const std::optional<float> Fraction = Physics->findPosedStop(Posed, MaxDepth, Pelvis);
    if (!Fraction) return std::nullopt;
    // The limbs of the strikers go back along their motion relative to the
    // pelvis (the clip's motion), whole, so a leg stays on its hip; the
    // pelvis and the other limbs keep this step's motion.
    if (*Fraction < 1.0f) {
        std::bitset<BodyPartCount> StrikingLimbs;
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            if (Strikers.test(Index)) StrikingLimbs.set(static_cast<size_t>(getLimbTop(static_cast<BodyPart>(Index))));
        }
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            if (StrikingLimbs.test(Index)) rewindLimb(static_cast<BodyPart>(Index), *Fraction);
        }
    }
    StoppedAtContact = true;
    StoppedParts = Strikers;
    return Fraction;
}

void Rig::holdLimbsBack(float MaxDepth) {
    HeldLimbs.reset();
    if (CurrentPosture == Posture::KnockedDown) return;
    const physics::Body Pelvis = getPart(Root).Handle;
    for (const auto& Joint : Joints) {
        const auto Top = static_cast<size_t>(Joint.Child);
        if (Joint.Parent != Root || !isKinematic(Joint.Child)) continue;
        std::vector<physics::Body> Limb;
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const auto Part = static_cast<BodyPart>(Index);
            if (Part != Root && isKinematic(Part) && getLimbTop(Part) == Joint.Child) Limb.push_back(Parts[Index].Handle);
        }
        // Touching the opponent is fine (1); only a limb that went too deep
        // goes back.
        const std::optional<float> Fraction = Physics->findPosedStop(Limb, MaxDepth, Pelvis);
        if (!Fraction || *Fraction >= 1.0f) continue;
        rewindLimb(Joint.Child, *Fraction);
        HeldLimbs.set(Top);
    }
}

void Rig::pushBody(float Delta) {
    const float Before = Controller.getPlannedX();
    Controller.shift(Delta);
    const float Drag = getFootDrag(Controller.getPlannedX()) - getFootDrag(Before);
    for (auto& Limb : Legs) {
        if (Limb.Locked) Limb.LockX += Drag;
    }
}

float Rig::getPosedPenetration(BodyPart Part) const {
    return isKinematic(Part) ? Physics->getPosedPenetration(getPart(Part).Handle) : 0.0f;
}

bool Rig::isKinematic(BodyPart Part) const {
    return getPart(Part).Kinematic && CurrentPosture != Posture::KnockedDown;
}

bool Rig::isYielding(BodyPart Part) const { return getPart(Part).Yielding; }

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

float Rig::getSoleHeight(const PerBodyPart<float>& Angles, BodyPart Foot) const {
    const PerBodyPart<float> Corrections = getAngleCorrections(Angles);
    const Placement OnFloor{.Position = {}, .Angle = Angles[static_cast<size_t>(Root)] * Facing};
    const PerBodyPart<Placement> Pose = computeTargetPose(OnFloor, Corrections);
    float Lowest = std::numeric_limits<float>::max();
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        if (Part.Kinematic) Lowest = std::min(Lowest, getLowestPoint(Part.Shape, Target.Position, Target.Angle));
    }
    const Placement& Sole = Pose[static_cast<size_t>(Foot)];
    return getLowestPoint(getPart(Foot).Shape, Sole.Position, Sole.Angle) - Lowest;
}

float LegStance::getSpread() const { return std::abs(Right.Ankle.X - Left.Ankle.X); }

LegStance Rig::measureLegs(const PerBodyPart<float>& Angles) const {
    const Placement OnFloor{.Position = {}, .Angle = Angles[static_cast<size_t>(Root)] * Facing};
    const PerBodyPart<Placement> Pose = computeTargetPose(OnFloor, getAngleCorrections(Angles));
    float Lowest = std::numeric_limits<float>::max();
    for (auto&& [Part, Target] : std::views::zip(Parts, Pose)) {
        if (Part.Kinematic) Lowest = std::min(Lowest, getLowestPoint(Part.Shape, Target.Position, Target.Angle));
    }
    // Lifted so that the lowest posed part touches the floor.
    LegStance Result{.PelvisHeight = -Lowest};
    for (const auto& Limb : Legs) {
        const Vec2 Ankle = getAnkleInPose(Limb, Pose);
        const Placement& Foot = Pose[static_cast<size_t>(Limb.Foot)];
        Result.getFoot(Limb.Foot) = {
            .Ankle = {Ankle.X * Facing, Ankle.Y - Lowest},
            .Angle = Foot.Angle * Facing,
            .SoleHeight = getLowestPoint(getPart(Limb.Foot).Shape, Foot.Position, Foot.Angle) - Lowest};
    }
    return Result;
}

LegStance Rig::measureLegsNow() const {
    const Vec2 Pelvis = getPart(Root).Handle.getPosition();
    LegStance Result{.PelvisHeight = Pelvis.Y};
    for (const auto& Limb : Legs) {
        const JointState& Ankle = Joints[Limb.Ankle];
        const Vec2 Hinge = getPart(Ankle.Parent).Handle.getWorldPoint(Ankle.AnchorInParent);
        const PartState& Foot = getPart(Limb.Foot);
        const Vec2 Position = Foot.Handle.getPosition();
        const float Angle = Foot.Handle.getAngle();
        Result.getFoot(Limb.Foot) = {.Ankle = {(Hinge.X - Pelvis.X) * Facing, Hinge.Y},
                                     .Angle = Angle * Facing,
                                     .SoleHeight = getLowestPoint(Foot.Shape, Position, Angle),
                                     .Planted = Limb.Locked};
    }
    return Result;
}

void Rig::reachFoot(PerBodyPart<float>& Angles, BodyPart Foot, float PelvisHeight, Vec2 Ankle,
                    float FootAngle) const {
    const auto Limb = std::ranges::find(Legs, Foot, &Leg::Foot);
    if (Limb == Legs.end()) return;
    PerBodyPart<float> Corrections = getAngleCorrections(Angles);
    const Placement OnFloor{.Position = {0.0f, PelvisHeight}, .Angle = Angles[static_cast<size_t>(Root)] * Facing};
    PerBodyPart<Placement> Pose = computeTargetPose(OnFloor, Corrections);
    Pose[static_cast<size_t>(Foot)].Angle = FootAngle * Facing;
    // The knee bends the way a knee bends (the middle of its range), also
    // when the pose has it straight: a straight knee would not lift a foot.
    const JointState& Knee = Joints[Limb->Knee];
    reachAnkle(*Limb, Pose, {Ankle.X * Facing, Ankle.Y}, Corrections, (Knee.LowerAngle + Knee.UpperAngle) * 0.5f);
    for (const size_t Index : {Limb->Hip, Limb->Knee, Limb->Ankle}) {
        const auto Child = static_cast<size_t>(Joints[Index].Child);
        Angles[Child] = (Joints[Index].Target + Corrections[Child]) * Facing;
    }
}

void Rig::keepFeetPlanted() {
    for (auto& Limb : Legs) {
        if (!Limb.Locked || Limb.Stepping) continue;
        Limb.KeptOffsetX = Limb.OffsetX;
        Limb.Kept = true;
    }
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
        UnjamParts.set(Index, State.Unjam);
        const bool PassThrough = Def.PassThrough.test(Index);
        const uint64_t Category = State.Kinematic ? PosedPartBit : PassThrough ? PassThroughBit : PhysicalPartBit;
        const uint64_t Mask = PassThrough ? CollideWithAll & ~PassThroughBit : CollideWithAll;
        State.Handle = PhysWorld.createBody({
            .Position = Setup.Origin + Center,
            .AngularDamping = Control.AngularDamping,
            .Part = physics::PartRef{Setup.FighterIndex, Source.Part},
        });
        PhysWorld.addShape(State.Handle, makeShapeDef(State.Shape, CollisionGroup, Category, Mask));

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
            PhysWorld.addShape(State.Handle, makeShapeDef(Blade, CollisionGroup, Category, Mask));
        }

        // The mass is set while the body is dynamic; the densities stay when
        // it becomes kinematic.
        State.Handle.setMass(Setup.MassKg[Index]);
        State.Mass = State.Handle.getMass();
        State.Inertia = State.Handle.getRotationalInertia();
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
        Joint.Wish = std::clamp(0.0f, Joint.LowerAngle, Joint.UpperAngle);
        Joint.Target = Joint.Wish;
        Joint.PreviousTarget = Joint.Target;
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
        const Vec2 ThighBone = Joints[*Knee].AnchorInParent + Joints[Hip].ChildFromAnchor;
        const Vec2 ShinBone = Joints[*Ankle].AnchorInParent + Joints[*Knee].ChildFromAnchor;
        Legs.push_back({.Hip = Hip,
                        .Knee = *Knee,
                        .Ankle = *Ankle,
                        .Foot = Joints[*Ankle].Child,
                        .Length = ThighBone.getLength() + ShinBone.getLength()});
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

Rig::Placement Rig::getStandingRoot(float RootX, const PerBodyPart<float>& Corrections) const {
    // Pose the body with the root on the floor line, then lift it so that
    // the lowest kinematic part (a sole) just touches the floor. Bent knees
    // (a crouch) leave the feet higher, so the pelvis goes down.
    const Placement OnFloor{.Position = {RootX, 0.0f},
                            .Angle = TargetAngles[static_cast<size_t>(Root)] * Facing};
    const PerBodyPart<Placement> Pose = computeTargetPose(OnFloor, Corrections);
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
    if (CurrentPosture != Posture::Standing) releaseFeet();
    const PerBodyPart<Placement> Pose = computePosedPose(Controller.getPositionX(), Legs, Dt, PostureSec, {});
    for (auto&& [Part, Goal] : std::views::zip(Parts, Pose)) {
        if (Part.Kinematic) Part.Handle.moveTo(Goal.Position, Goal.Angle, Dt);
    }
}

PerBodyPart<Rig::Placement> Rig::computePosedPose(float RootX, std::vector<Leg>& Limbs, float Dt,
                                                   float PostureTime, const PerBodyPart<float>& Corrections) const {
    Placement RootPlacement = getStandingRoot(RootX, Corrections);
    PerBodyPart<Placement> Pose = computeTargetPose(RootPlacement, Corrections);
    if (CurrentPosture == Posture::Standing) {
        // A kept foot planted wider than the clip's pose reaches: the pelvis
        // goes down so that the leg reaches it, instead of dragging it.
        const float RootDrop = getReachDrop(Pose, Limbs);
        if (RootDrop > 0.0f) {
            RootPlacement.Position.Y -= RootDrop;
            Pose = computeTargetPose(RootPlacement, Corrections);
        }
        Pose = computeTargetPose(RootPlacement, plantFeet(Pose, Limbs, Dt, Corrections, RootDrop));
    }
    if (CurrentPosture != Posture::GettingUp) return Pose;
    // Getting up blends from where the parts lay to the stance.
    const float Blend = smoothStep(std::clamp(PostureTime / Control.GetUpSec, 0.0f, 1.0f));
    for (auto&& [Goal, From] : std::views::zip(Pose, GetUpFrom)) {
        Goal.Position = lerp(From.Position, Goal.Position, Blend);
        Goal.Angle = From.Angle + wrapAngle(Goal.Angle - From.Angle) * Blend;
    }
    return Pose;
}

std::vector<PartPlacement> Rig::predictBody(float RootX, float Dt) const {
    std::vector<PartPlacement> Result;
    if (CurrentPosture == Posture::KnockedDown) return Result;
    // applyControl() advances the posture time before it poses the parts.
    // A push away from the plan takes the planted feet along (pushBody()).
    std::vector<Leg> Limbs = Legs;
    const float Pushed = getFootDrag(RootX) - getFootDrag(Controller.getPlannedX());
    for (auto& Limb : Limbs) {
        if (Limb.Locked) Limb.LockX += Pushed;
    }
    // The posed joints follow the share of the planned travel made
    // (setTravelPose()).
    const PerBodyPart<Placement> Pose =
        computePosedPose(RootX, Limbs, Dt, PostureSec + Dt,
                         getTravelCorrections(Controller.getTravelShare(RootX, PoseTravel)));
    // The parts that are not posed now (the torso and the head, held on the
    // pelvis) and the strikers (they stop at the opponent by themselves,
    // stopAtContact(); the opponent must not walk into where they are) move
    // rigidly with the pelvis.
    const PartState& Pelvis = getPart(Root);
    const Placement& PelvisGoal = Pose[static_cast<size_t>(Root)];
    const float Turn = PelvisGoal.Angle - Pelvis.Handle.getAngle();
    const auto carry = [&](const PartState& Part) {
        const Vec2 FromPelvis = rotate(Part.Handle.getPosition() - Pelvis.Handle.getPosition(), Turn);
        return PartPlacement{.Handle = Part.Handle,
                             .Position = PelvisGoal.Position + FromPelvis,
                             .Angle = Part.Handle.getAngle() + Turn};
    };
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const PartState& Part = Parts[Index];
        if (Part.Unjam) continue;
        const Placement& Placed = Pose[Index];
        if (StrikingParts.test(Index) || !Part.Kinematic) {
            PartPlacement Carried = carry(Part);
            if (StrikingParts.test(Index) && Part.Kinematic) {
                Carried.Striking = true;
                Carried.PosedPosition = Placed.Position;
                Carried.PosedAngle = Placed.Angle;
            }
            Result.push_back(Carried);
            continue;
        }
        Result.push_back({.Handle = Part.Handle, .Position = Placed.Position, .Angle = Placed.Angle});
        // A lifted foot comes down where it is: the spacing keeps the floor
        // below it clear too, so that it does not step onto the opponent's
        // foot (in a side view the feet are in one plane). A kicking foot
        // goes back instead.
        const bool IsFoot = std::ranges::any_of(Legs, [&](const Leg& Limb) {
            return static_cast<size_t>(Limb.Foot) == Index;
        });
        const float Lift = getLowestPoint(Part.Shape, Placed.Position, Placed.Angle);
        if (IsFoot && Lift > 0.0f && !AttackingParts.test(Index)) {
            Result.push_back(
                {.Handle = Part.Handle, .Position = Placed.Position - Vec2{0.0f, Lift}, .Angle = Placed.Angle});
        }
    }
    return Result;
}

float Rig::measureGap(std::span<const PartPlacement> Own, std::span<const PartPlacement> Other) const {
    float Smallest = std::numeric_limits<float>::max();
    for (const auto& Mine : Own) {
        for (const auto& Theirs : Other) {
            float Gap = Physics->getGapAt(Mine.Handle, Mine.Position, Mine.Angle, Theirs.Handle, Theirs.Position,
                                          Theirs.Angle);
            if (Gap < 0.0f && Mine.Striking) {
                Gap = std::max(Gap, Physics->getGapAt(Mine.Handle, Mine.PosedPosition, Mine.PosedAngle, Theirs.Handle,
                                                      Theirs.Position, Theirs.Angle));
            }
            if (Gap < 0.0f && Theirs.Striking) {
                Gap = std::max(Gap, Physics->getGapAt(Mine.Handle, Mine.Position, Mine.Angle, Theirs.Handle,
                                                      Theirs.PosedPosition, Theirs.PosedAngle));
            }
            Smallest = std::min(Smallest, Gap);
        }
    }
    return Smallest;
}

float Rig::getReachDrop(const PerBodyPart<Placement>& Pose, const std::vector<Leg>& Limbs) const {
    float RootDrop = 0.0f;
    for (const auto& Limb : Limbs) {
        if (!Limb.Locked || !Limb.Kept) continue;
        const JointState& Hip = Joints[Limb.Hip];
        const Placement& Pelvis = Pose[static_cast<size_t>(Hip.Parent)];
        const Vec2 HipPoint = Pelvis.Position + rotate(Hip.AnchorInParent, Pelvis.Angle);
        const float Reach = Limb.Length - KeptReachMargin;
        const float Across = Limb.LockX - HipPoint.X;
        if (std::abs(Across) >= Reach) continue;   // out of reach at any height: it is dragged
        const float HighestHip = getAnkleInPose(Limb, Pose).Y + std::sqrt(Reach * Reach - Across * Across);
        RootDrop = std::max(RootDrop, HipPoint.Y - HighestHip);
    }
    return RootDrop;
}

PerBodyPart<float> Rig::plantFeet(const PerBodyPart<Placement>& Pose, std::vector<Leg>& Limbs, float Dt,
                                  const PerBodyPart<float>& Base, float RootDrop) const {
    // Standing still after a push, the feet are left away from the stance:
    // the foot farthest off steps back under the body, one at a time and
    // only while all feet stand (not during a kick).
    const bool Idle = Controller.getWalkVelocity() == 0.0f && std::abs(Controller.getKnockback()) < MinRestepKnockback;
    const bool AllPlanted = std::ranges::all_of(Limbs, &Leg::Locked);
    Leg* Farthest = nullptr;
    for (auto& Limb : Limbs) {
        if (Limb.Locked && (!Farthest || getRestepDistance(Limb) > getRestepDistance(*Farthest))) Farthest = &Limb;
    }
    if (Idle && AllPlanted && Farthest && Control.FootRestepDistance > 0.0f &&
        getRestepDistance(*Farthest) > Control.FootRestepDistance) {
        Farthest->Stepping = true;
        Farthest->Locked = false;
        Farthest->KeptOffsetX = 0.0f;
        Farthest->Kept = false;
    }

    PerBodyPart<float> Corrections = Base;
    for (auto& Limb : Limbs) {
        const JointState& Ankle = Joints[Limb.Ankle];
        const Placement& Shin = Pose[static_cast<size_t>(Ankle.Parent)];
        const Vec2 ClipAnkle = Shin.Position + rotate(Ankle.AnchorInParent, Shin.Angle);
        const PartState& Foot = getPart(Limb.Foot);
        const Placement& FootPose = Pose[static_cast<size_t>(Limb.Foot)];
        // The clip plants the foot when its sole is on the floor; a foot
        // stepping back plants when it is there.
        const bool Planted = getLowestPoint(Foot.Shape, FootPose.Position, FootPose.Angle) <= Control.FootPlantHeight;
        if (Limb.Stepping && std::abs(Limb.OffsetX) < StepDoneDistance) Limb.Stepping = false;

        if (Planted && !Limb.Locked && !Limb.Stepping) {
            Limb.Locked = true;
            Limb.LockX = ClipAnkle.X + Limb.OffsetX;
        } else if (!Planted && Limb.Locked) {
            Limb.Locked = false;   // lifted: it returns to the clip from where it stood
            Limb.KeptOffsetX = 0.0f;
            Limb.Kept = false;
        }
        float Lift = 0.0f;
        // A planted foot stays on the floor when the pelvis went down for it.
        if (Limb.Locked) Lift = RootDrop;
        if (Limb.Locked) {
            // A pull longer than the slip (a knockback, a push) drags the
            // foot, and so does one the leg cannot reach.
            const JointState& Hip = Joints[Limb.Hip];
            const Placement& Pelvis = Pose[static_cast<size_t>(Hip.Parent)];
            const Vec2 HipPoint = Pelvis.Position + rotate(Hip.AnchorInParent, Pelvis.Angle);
            const float Reach = Limb.Length - LegReachMargin;
            const float Drop = HipPoint.Y - ClipAnkle.Y - RootDrop;
            const float Span = std::sqrt(std::max(Reach * Reach - Drop * Drop, 0.0f));
            // A kept foot (keepFeetPlanted) holds its place as far as the leg
            // reaches: the legs rest where they stopped.
            const float Slip = Limb.Kept ? std::numeric_limits<float>::max() : Control.FootLockSlip;
            Limb.LockX = std::clamp(Limb.LockX, std::max(ClipAnkle.X - Slip, std::min(HipPoint.X - Span, ClipAnkle.X)),
                                    std::min(ClipAnkle.X + Slip, std::max(HipPoint.X + Span, ClipAnkle.X)));
            Limb.OffsetX = Limb.LockX - ClipAnkle.X;
        } else {
            Limb.OffsetX *= std::exp(-Control.FootLockRelease * Dt);
            // A step back to the stance lifts the foot off the floor.
            if (Limb.Stepping) Lift = std::abs(Limb.OffsetX) * Control.FootStepLift;
        }
        if (std::abs(Limb.OffsetX) > MinFootOffset || Lift > 0.0f) {
            reachAnkle(Limb, Pose, {ClipAnkle.X + Limb.OffsetX, ClipAnkle.Y + Lift}, Corrections,
                       Joints[Limb.Knee].Target);
        }
    }
    return Corrections;
}

void Rig::reachAnkle(const Leg& Limb, const PerBodyPart<Placement>& Pose, Vec2 Ankle,
                     PerBodyPart<float>& Corrections, float KneeHint) const {
    const JointState& Hip = Joints[Limb.Hip];
    const JointState& Knee = Joints[Limb.Knee];
    const JointState& AnkleJoint = Joints[Limb.Ankle];
    const Placement& Pelvis = Pose[static_cast<size_t>(Hip.Parent)];
    const Placement& Foot = Pose[static_cast<size_t>(AnkleJoint.Child)];

    // The bones from hinge to hinge in their bodies' frames (reference pose).
    const Vec2 ThighBone = Knee.AnchorInParent + Hip.ChildFromAnchor;
    const Vec2 ShinBone = AnkleJoint.AnchorInParent + Knee.ChildFromAnchor;
    const float ThighLength = ThighBone.getLength();
    const float ShinLength = ShinBone.getLength();
    const Vec2 HipPoint = Pelvis.Position + rotate(Hip.AnchorInParent, Pelvis.Angle);
    const Vec2 ToAnkle = Ankle - HipPoint;

    // Two-bone IK. The knee angle sets the hip-to-ankle distance:
    // |thigh + rotate(shin, knee)| = distance, two solutions. Keep the one
    // closest to the clip's knee within the limits; a target out of reach
    // gets the nearest possible knee, and the thigh still aims at it.
    const float Distance = ToAnkle.getLength();
    const float Cosine = (Distance * Distance - ThighLength * ThighLength - ShinLength * ShinLength) /
                         (2.0f * ThighLength * ShinLength);
    const float Opening = std::acos(std::clamp(Cosine, -1.0f, 1.0f));
    const float RestBend = getHeading(ShinBone) - getHeading(ThighBone);
    // A solution the knee cannot bend to (clamped to its limit) leaves the
    // ankle off the target: the one within the limits wins, else the one
    // closer to the hint.
    const auto clampKnee = [&](float Angle) {
        return std::clamp(wrapAngle(Angle), Knee.LowerAngle, Knee.UpperAngle);
    };
    const float RawA = wrapAngle(Opening - RestBend);
    const float RawB = wrapAngle(-Opening - RestBend);
    const float KneeA = clampKnee(RawA);
    const float KneeB = clampKnee(RawB);
    const bool ReachesA = KneeA == RawA;
    const bool ReachesB = KneeB == RawB;
    const bool CloserA = std::abs(KneeA - KneeHint) <= std::abs(KneeB - KneeHint);
    const float KneeAngle = ReachesA != ReachesB ? (ReachesA ? KneeA : KneeB) : (CloserA ? KneeA : KneeB);

    // Body angles, then joint angles.
    const float ThighAngle = getHeading(ToAnkle) - getHeading(ThighBone + rotate(ShinBone, KneeAngle));
    const float ShinAngle = ThighAngle + KneeAngle;
    const auto setCorrection = [&](const JointState& Joint, float Angle) {
        Corrections[static_cast<size_t>(Joint.Child)] =
            std::clamp(wrapAngle(Angle), Joint.LowerAngle, Joint.UpperAngle) - Joint.Target;
    };
    setCorrection(Hip, ThighAngle - Pelvis.Angle);
    setCorrection(Knee, KneeAngle);
    setCorrection(AnkleJoint, Foot.Angle - ShinAngle);   // the foot keeps its angle to the floor
}

PerBodyPart<float> Rig::getAngleCorrections(const PerBodyPart<float>& Angles) const {
    PerBodyPart<float> Corrections{};
    for (const auto& Joint : Joints) {
        const float Angle = std::clamp(Angles[static_cast<size_t>(Joint.Child)] * Facing, Joint.LowerAngle,
                                       Joint.UpperAngle);
        Corrections[static_cast<size_t>(Joint.Child)] = Angle - Joint.Target;
    }
    return Corrections;
}

Vec2 Rig::getAnkleInPose(const Leg& Limb, const PerBodyPart<Placement>& Pose) const {
    const JointState& Ankle = Joints[Limb.Ankle];
    const Placement& Shin = Pose[static_cast<size_t>(Ankle.Parent)];
    return Shin.Position + rotate(Ankle.AnchorInParent, Shin.Angle);
}

float Rig::getRestepDistance(const Leg& Limb) { return std::abs(Limb.OffsetX - Limb.KeptOffsetX); }

BodyPart Rig::getLimbTop(BodyPart Part) const {
    for (const JointState* Joint = findJoint(Part); Joint && Joint->Parent != Root; Joint = findJoint(Joint->Parent)) {
        Part = Joint->Parent;
    }
    return Part;
}

void Rig::rewindLimb(BodyPart Top, float Fraction) {
    const physics::Body Pelvis = getPart(Root).Handle;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        if (Part != Root && Parts[Index].Kinematic && getLimbTop(Part) == Top) {
            Physics->rewindBody(Parts[Index].Handle, Fraction, Pelvis);
        }
    }
}

float Rig::getFootDrag(float RootX) const {
    // Between where the pelvis is and where it planned to go, a correction
    // only takes back some of its own travel: the planted feet stay.
    const float Start = Controller.getPositionX();
    const float Planned = Start + Controller.getPlannedTravel();
    return RootX - std::clamp(RootX, std::min(Start, Planned), std::max(Start, Planned));
}

PerBodyPart<float> Rig::getTravelCorrections(float Share) const {
    PerBodyPart<float> Corrections{};
    for (const auto& Joint : Joints) {
        Corrections[static_cast<size_t>(Joint.Child)] = (Joint.StillWish - Joint.Wish) * (1.0f - Share);
    }
    return Corrections;
}

void Rig::followTravel() {
    const float Share = getTravelShare();
    for (auto& Joint : Joints) {
        Joint.Wish = Joint.StillWish + (Joint.Wish - Joint.StillWish) * Share;
        Joint.StillWish = Joint.Wish;
    }
    refreshTargets();
}

void Rig::releaseFeet() {
    for (auto& Limb : Legs) {
        Limb.Locked = false;
        Limb.Stepping = false;
        Limb.OffsetX = 0.0f;
        Limb.KeptOffsetX = 0.0f;
        Limb.Kept = false;
    }
}

void Rig::carryPhysicalParts(Vec2 OldVelocity, float OldSpin) {
    // A passenger in a bus swings when the bus starts, stops or turns: the
    // joints drag the physical parts after the pelvis only once it has
    // moved. Instead the parts take the change of the pelvis motion at once,
    // as if they were rigidly on it, and keep their motion relative to it:
    // a hit's push and the motors' work stay. The rigid motion of the
    // pelvis is a velocity field v(p) = V + W x (p - C).
    const physics::Body Pelvis = getPart(Root).Handle;
    const Vec2 Center = Pelvis.getWorldCenterOfMass();
    Vec2 Change = Pelvis.getLinearVelocity() - OldVelocity;
    float SpinChange = Pelvis.getAngularVelocity() - OldSpin;

    // The knockback that really moved the pelvis (a wall or the opponent may
    // have stopped it) is carried only by its share: without it the upper
    // body lags behind the push and shows the hit.
    const float Realized = getRealizedShare(PlannedKnockback, Controller.getVelocity() - Controller.getWalkVelocity());
    Change.X -= (1.0f - Control.KnockbackTransfer) * (Realized - CarriedKnockback);
    CarriedKnockback = Realized;

    Change *= Control.CarrierTransfer;
    SpinChange *= Control.CarrierTransfer;
    for (auto& Part : Parts) {
        if (Part.Kinematic) continue;
        const Vec2 FromCenter = Part.Handle.getWorldCenterOfMass() - Center;
        Part.Handle.setLinearVelocity(Part.Handle.getLinearVelocity() + Change + perp(FromCenter) * SpinChange);
        Part.Handle.setAngularVelocity(Part.Handle.getAngularVelocity() + SpinChange);
    }
}

float Rig::getCarriedGravityScale() const {
    // Standing, the pelvis carries the weight of the upper body: the motors
    // only pose it. A ragdoll and a body getting up from one feel it all.
    return CurrentPosture == Posture::Standing ? 1.0f - Control.GravityCompensation : 1.0f;
}

void Rig::driveMotors(float Dt) {
    const float Stiffness = getStiffness();
    // A velocity motor whose speed is the clip's own joint speed
    // (feed-forward) plus the angle error times the gain follows a moving
    // pose without lag and closes an error like a critically damped spring,
    // as long as its torque suffices. The torque limit is the larger of:
    //  - strength: the profile's torque (STR) times the joint's share and
    //    the stiffness, what the body resists a hit with and strikes with;
    //  - holding (getHoldTorque): the floor that a pose is always reached
    //    without overshoot and held, whatever the mass of the child chain
    //    (CON, armour) and however weak the fighter; it falls with the
    //    stiffness squared through the gain, so a hit softens it too.
    // A stiff joint snaps to its target, a weak one lags and gives way; a
    // yielding limb is softer still. A ragdoll keeps only the strength part,
    // at its low stiffness.
    const bool Holding = CurrentPosture != Posture::KnockedDown;
    for (auto& Joint : Joints) {
        const float TargetSpeed = Dt > 0.0f ? (Joint.Target - Joint.PreviousTarget) / Dt : 0.0f;
        Joint.PreviousTarget = Joint.Target;
        if (isKinematic(Joint.Child)) {
            Joint.HoldTorque = 0.0f;
            Joint.Handle.setMotorSpeed(0.0f);
            Joint.Handle.setMaxMotorTorque(0.0f);
            continue;
        }
        // Knocked down, the legs (posed while standing) are softer still:
        // a ragdoll standing on stiff legs spread in the stance could stay
        // up as a trestle instead of falling.
        const PartState& Child = getPart(Joint.Child);
        const float JointStiffness = Child.Yielding               ? Stiffness * Control.YieldStiffness
                                     : !Holding && Child.Kinematic ? Stiffness * Control.KnockdownLegStiffness
                                                                   : Stiffness;
        const float Gain = MotorGain * JointStiffness;
        const float FeedForward = Holding ? Control.FeedForward * TargetSpeed : 0.0f;
        const float Speed = FeedForward + (Joint.Target - Joint.Handle.getAngle()) * Gain;
        Joint.Handle.setMotorSpeed(std::clamp(Speed, -Control.MaxJointSpeed, Control.MaxJointSpeed));
        Joint.HoldTorque = Holding ? getHoldTorque(Joint, Gain) : 0.0f;
        Joint.Handle.setMaxMotorTorque(std::max(MotorMaxTorque * Joint.Strength * JointStiffness, Joint.HoldTorque));
    }
}

float Rig::getHoldTorque(const JointState& Joint, float Gain) const {
    // The child chain: the child and every part below it. Parents come
    // before children, so one pass finds it.
    std::bitset<BodyPartCount> Chain;
    Chain.set(static_cast<size_t>(Joint.Child));
    for (const auto& Other : Joints) {
        if (Chain.test(static_cast<size_t>(Other.Parent))) Chain.set(static_cast<size_t>(Other.Child));
    }
    // The moment of its mass and its inertia of the chain about the hinge, as the
    // chain stands now.
    const Vec2 Hinge = Joint.Handle.getAnchor();
    Vec2 MassMoment;
    float Inertia = 0.0f;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Chain.test(Index)) continue;
        const PartState& Part = Parts[Index];
        const Vec2 FromHinge = Part.Handle.getWorldCenterOfMass() - Hinge;
        MassMoment += FromHinge * Part.Mass;
        Inertia += Part.Inertia + Part.Mass * FromHinge.getLengthSquared();
    }
    // A velocity motor at gain G decelerates the chain from the speed G * e
    // over the time 1/G: it needs the angular acceleration G^2 * e to stop
    // at the target without overshoot. The weight is the worst case, the
    // chain held out sideways (its lever is the distance to its center).
    const float Damping = Inertia * Gain * Gain * Control.DampedErrorAngle;
    const float Weight = Control.HoldGravityMargin * getCarriedGravityScale() * Physics->getGravity().getLength() *
                         MassMoment.getLength();
    return Damping + Weight;
}

PoseError Rig::getPoseError() const {
    const PartState& Pelvis = getPart(Root);
    const PerBodyPart<Placement> Pose =
        computeTargetPose({.Position = Pelvis.Handle.getPosition(), .Angle = Pelvis.Handle.getAngle()});
    PoseError Worst;
    for (auto&& [Index, Part, Target] : std::views::zip(std::views::iota(size_t{0}), Parts, Pose)) {
        if (Part.Kinematic) continue;
        const float Error = std::abs(wrapAngle(Part.Handle.getAngle() - Target.Angle));
        if (Error <= Worst.Angle) continue;
        Worst = {.Angle = Error, .Part = static_cast<BodyPart>(Index)};
    }
    return Worst;
}

void Rig::updateJams(float Dt) {
    // Two motors pushing limbs into each other hold a deadlock forever: each
    // is at its torque limit, and nothing in the pose changes. A limb that
    // touches the opponent and stays far from its target for JamSec yields:
    // its motors soften and it pulls back to the yield pose. It still
    // collides, so nothing passes through; the solver pushes it out of the
    // way. It yields at least YieldSec, then until the clip's pose of it is
    // clear of the opponent: a guard that does not fit at close range stays
    // tucked instead of jamming again and again.
    if (CurrentPosture == Posture::KnockedDown) return;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        PartState& Limb = Parts[Index];
        const auto LimbPart = static_cast<BodyPart>(Index);
        if (!Limb.Unjam || Limb.Limb != LimbPart) continue;

        bool Touching = false;
        float Error = 0.0f;
        for (const auto& Joint : Joints) {
            const PartState& Part = getPart(Joint.Child);
            if (!Part.Unjam || Part.Limb != LimbPart) continue;
            Touching = Touching || Physics->isTouchingOtherFighter(Part.Handle);
            Error = std::max(Error, std::abs(Joint.Wish - Joint.Handle.getAngle()));
        }
        if (Limb.Yielding) {
            // An attack that asks the limb for something new (a strike)
            // tries again at once. Anything else (a step, a guard) waits
            // until the clip's pose of the limb is clear of the opponent by
            // more than a touch: a guard does not come back into a fighter
            // that is still close and jam again (hysteresis).
            Limb.YieldSec += Dt;
            const bool NewStrike = std::ranges::any_of(Joints, [&](const JointState& Joint) {
                const auto Child = static_cast<size_t>(Joint.Child);
                const PartState& Part = Parts[Child];
                return Part.Unjam && Part.Limb == LimbPart && AttackingParts.test(Child) &&
                       std::abs(Joint.Wish - Joint.YieldWish) > Control.JamAngle;
            });
            if (NewStrike || (Limb.YieldSec >= Control.YieldSec && !isWishBlocked(LimbPart))) {
                setLimbYielding(LimbPart, false);
            }
            continue;
        }
        Limb.StuckSec = Touching && Error > Control.JamAngle ? Limb.StuckSec + Dt : 0.0f;
        if (Limb.StuckSec >= Control.JamSec) setLimbYielding(LimbPart, true);
    }
}

void Rig::setLimbYielding(BodyPart Limb, bool Yielding) {
    getPart(Limb).StuckSec = 0.0f;
    getPart(Limb).YieldSec = 0.0f;
    for (auto& Part : Parts) {
        if (Part.Unjam && Part.Limb == Limb) Part.Yielding = Yielding;
    }
    for (auto& Joint : Joints) {
        const PartState& Part = getPart(Joint.Child);
        if (Part.Unjam && Part.Limb == Limb) Joint.YieldWish = Joint.Wish;
    }
    refreshTargets();
    // A limb that starts or stops yielding eases to its new target: the
    // jump of the target is not a joint speed of the clip.
    for (auto& Joint : Joints) {
        const PartState& Part = getPart(Joint.Child);
        if (Part.Unjam && Part.Limb == Limb) Joint.PreviousTarget = Joint.Target;
    }
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} {}", FighterIndex + 1, getBodyPartName(Limb),
                                    Yielding ? "stuck in the opponent: yields" : "stops yielding"));
    }
}

bool Rig::isWishBlocked(BodyPart Limb) const {
    // Forward kinematics of the limb at the clip's angles; the parent of
    // its topmost part stays where it is.
    PerBodyPart<Placement> Pose{};
    std::bitset<BodyPartCount> Placed;
    for (const auto& Joint : Joints) {
        const PartState& Child = getPart(Joint.Child);
        if (!Child.Unjam || Child.Limb != Limb) continue;
        const auto ParentIndex = static_cast<size_t>(Joint.Parent);
        const PartState& Parent = Parts[ParentIndex];
        const Placement From = Placed.test(ParentIndex) ? Pose[ParentIndex]
                                                        : Placement{.Position = Parent.Handle.getPosition(),
                                                                    .Angle = Parent.Handle.getAngle()};
        const float Angle = From.Angle + Joint.Wish;
        const Vec2 Anchor = From.Position + rotate(Joint.AnchorInParent, From.Angle);
        const auto ChildIndex = static_cast<size_t>(Joint.Child);
        Pose[ChildIndex] = {.Position = Anchor + rotate(Joint.ChildFromAnchor, Angle), .Angle = Angle};
        Placed.set(ChildIndex);
        if (Physics->isOverlappingOtherFighterAt(Child.Handle, Pose[ChildIndex].Position, Angle, Control.YieldReturnClearance,
                                                 UnjamParts)) {
            return true;
        }
    }
    return false;
}

void Rig::refreshTargets() {
    for (auto& Joint : Joints) {
        const auto Child = static_cast<size_t>(Joint.Child);
        Joint.Target = Joint.Wish;
        if (Parts[Child].Yielding && YieldPosed.test(Child)) {
            Joint.Target = std::clamp(YieldAngles[Child] * Facing, Joint.LowerAngle, Joint.UpperAngle);
        }
    }
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
        Part.Yielding = false;
        Part.StuckSec = 0.0f;
    }
    refreshTargets();
    // The whole body takes the momentum of the hit as one rigid body: every
    // part moves with the push plus the spin about the center of mass. The
    // motion the parts had is dropped: a physical part hit by a kinematic leg
    // moves as fast as the leg, which says nothing about the whole body.
    const Vec2 Center = getCenterOfMass();
    for (auto& Part : Parts) {
        Part.Handle.setLinearVelocity(Velocity + perp(Part.Handle.getWorldCenterOfMass() - Center) * Spin);
        Part.Handle.setAngularVelocity(Spin);
    }
    Controller.reset(getPartPosition(Root).X);
}

void Rig::startGettingUp() {
    CurrentPosture = Posture::GettingUp;
    PostureSec = 0.0f;
    for (auto&& [Part, From] : std::views::zip(Parts, GetUpFrom)) {
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
    // The targets are mirrored, not moved: no joint speed for the motors.
    for (auto& Joint : Joints) Joint.PreviousTarget = Joint.Target;
    releaseFeet();

    // The arms now reach to the other side, maybe into the opponent behind:
    // a limb that overlaps it yields, and the solver pushes it out.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto LimbPart = static_cast<BodyPart>(Index);
        const PartState& Limb = Parts[Index];
        if (!Limb.Unjam || Limb.Limb != LimbPart) continue;
        const bool Overlapping = std::ranges::any_of(Parts, [&](const PartState& Part) {
            return Part.Unjam && Part.Limb == LimbPart && Physics->isOverlappingOtherFighter(Part.Handle);
        });
        if (Overlapping) setLimbYielding(LimbPart, true);
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
        // A physical part away from its ghost: a line from where it is to
        // where the motors pull it, with the angle off.
        if (Part.Kinematic) continue;
        const float Error = wrapAngle(Part.Handle.getAngle() - Target.Angle);
        if (std::abs(Error) < MinDrawnPoseError) continue;
        debug::drawLine(debug::Cat::TargetPose, Part.Handle.getPosition(), Target.Position);
        debug::drawText(debug::Cat::TargetPose, Part.Handle.getPosition(),
                        std::format("{:+.0f} deg", Error * 180.0f / Pi));
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
    // Limbs that yield to the opponent.
    for (const auto& Part : Parts) {
        if (!Part.Yielding) continue;
        drawShape(debug::Cat::Contacts, Part.Shape, Part.Handle.getPosition(), Part.Handle.getAngle());
        debug::drawText(debug::Cat::Contacts, Part.Handle.getPosition(), "yields");
    }
    // Posed strikers held at the opponent's posed parts in the last step.
    if (StoppedAtContact) {
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const PartState& Part = Parts[Index];
            if (!StoppedParts.test(Index) || !isKinematic(static_cast<BodyPart>(Index))) continue;
            drawShape(debug::Cat::Contacts, Part.Shape, Part.Handle.getPosition(), Part.Handle.getAngle());
            debug::drawText(debug::Cat::Contacts, Part.Handle.getPosition(), "stopped");
        }
    }
    // Posed limbs held back from the opponent in the last step.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        if (!isKinematic(Part) || Part == Root || !HeldLimbs.test(static_cast<size_t>(getLimbTop(Part)))) continue;
        const PartState& Held = Parts[Index];
        drawShape(debug::Cat::Contacts, Held.Shape, Held.Handle.getPosition(), Held.Handle.getAngle());
        if (HeldLimbs.test(Index)) debug::drawText(debug::Cat::Contacts, Held.Handle.getPosition(), "held back");
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
        Feet += std::format("{} {} {:+.2f}", getBodyPartName(Limb.Foot),
                            Limb.Locked ? "planted" : Limb.Stepping ? "steps back" : "free",
                            Limb.OffsetX);
        if (Limb.KeptOffsetX != 0.0f) Feet += std::format(" (kept {:+.2f})", Limb.KeptOffsetX);
    }
    debug::setPanel(Name + " feet", Feet);

    std::string Limbs;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const PartState& Limb = Parts[Index];
        if (!Limb.Unjam || Limb.Limb != static_cast<BodyPart>(Index)) continue;
        if (!Limbs.empty()) Limbs += ", ";
        Limbs += std::format("{} {}", getBodyPartName(static_cast<BodyPart>(Index)),
                             Limb.Yielding        ? std::format("yields {:.2f} s", Limb.YieldSec)
                             : Limb.StuckSec > 0.0f ? std::format("stuck {:.2f} s", Limb.StuckSec)
                                                    : "ok");
    }
    if (StayDown) Limbs += Limbs.empty() ? "stays down" : ", stays down";
    debug::setPanel(Name + " limbs", Limbs);

    // How closely the physical upper body follows the clip, and the largest
    // holding part of a motor's torque limit.
    const PoseError Error = getPoseError();
    float Hold = 0.0f;
    BodyPart HoldPart = Root;
    for (const auto& Joint : Joints) {
        if (Joint.HoldTorque <= Hold) continue;
        Hold = Joint.HoldTorque;
        HoldPart = Joint.Child;
    }
    debug::setPanel(Name + " pose", std::format("error {:.0f} deg ({}), carried: {}, hold {} {:.0f} N*m",
                                                Error.Angle * 180.0f / Pi, getBodyPartName(Error.Part),
                                                Carrying ? "yes" : "no", getBodyPartName(HoldPart), Hold));

    // Posed parts sinking into the opponent's posed parts: nothing in
    // physics keeps them apart (only a stopped strike and the pelvis spacing).
    float Overlap = 0.0f;
    BodyPart Deepest = Root;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        const float Depth = getPosedPenetration(Part);
        if (Depth <= Overlap) continue;
        Overlap = Depth;
        Deepest = Part;
    }
    std::string Held;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (HeldLimbs.test(Index)) Held += std::format(", {} held back", getBodyPartName(static_cast<BodyPart>(Index)));
    }
    debug::setPanel(Name + " posed overlap", Overlap > 0.0f ? std::format("{} {:.3f} m{}{}", getBodyPartName(Deepest),
                                                                          Overlap, StoppedAtContact ? ", stopped" : "",
                                                                          Held)
                                                            : Held.empty() ? "-" : Held.substr(2));
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

/// The part of \p Part (a velocity, signed) that \p Realized (the velocity
/// that really happened, signed) contains: none if they point apart, at
/// most all of \p Realized.
float getRealizedShare(float Part, float Realized) {
    if (Part * Realized <= 0.0f) return 0.0f;
    return Part > 0.0f ? std::min(Part, Realized) : std::max(Part, Realized);
}

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
