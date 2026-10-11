#include "editor/arm_ik.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>
#include <stdexcept>

namespace fighter::editor {
namespace {

constexpr float Pi = std::numbers::pi_v<float>;
constexpr float FullTurn = 2.0f * Pi;
/// A fist this close to its target has reached it, m.
constexpr float ReachTolerance = 1e-3f;

const rig::JointDef* findJoint(const rig::RigDef& Def, BodyPart Child);
const rig::JointDef& getJoint(const rig::RigDef& Def, BodyPart Child);
/// \p Angle wrapped into [-pi, pi].
float wrapAngle(float Angle);
float getDirection(Vec2 Vector);
bool isInside(float Angle, AngleRange Range);

} // namespace

Vec2 getJointPivot(const rig::RigDef& Def, const PosedBody& Body, BodyPart Part) {
    const rig::JointDef* Joint = findJoint(Def, Part);
    if (!Joint) return Body.get(Part).Position;
    const PosedPart& Parent = Body.get(Joint->Parent);
    return Parent.Position + rotate(Joint->Anchor - getPartOrigin(Def.getPart(Joint->Parent)), Parent.Angle);
}

Vec2 getFistPoint(const rig::RigDef& Def, const PosedBody& Body, BodyPart Forearm) {
    const rig::PartDef& Part = Def.getPart(Forearm);
    const PosedPart& Posed = Body.get(Forearm);
    return Posed.Position + rotate(Part.End - getPartOrigin(Part), Posed.Angle);
}

float getPosedJointAngle(const rig::RigDef& Def, const PosedBody& Body, BodyPart Part) {
    const rig::JointDef* Joint = findJoint(Def, Part);
    if (!Joint) return Body.get(Part).Angle;
    return Body.get(Part).Angle - Body.get(Joint->Parent).Angle;
}

float clampJointAngle(float Angle, AngleRange Range) {
    for (const float Turn : {0.0f, -FullTurn, FullTurn}) {
        if (isInside(Angle + Turn, Range)) return Angle + Turn;
    }
    const float ToLower = std::abs(wrapAngle(Angle - Range.Lower));
    const float ToUpper = std::abs(wrapAngle(Angle - Range.Upper));
    return ToLower <= ToUpper ? Range.Lower : Range.Upper;
}

ArmSolution solveArmIk(const rig::RigDef& Def, const PosedBody& Body, BodyPart Forearm, Vec2 Target,
                       ElbowBend Preferred) {
    const rig::JointDef& Elbow = getJoint(Def, Forearm);
    const rig::JointDef& Shoulder = getJoint(Def, Elbow.Parent);
    const Vec2 Pivot = getJointPivot(Def, Body, Elbow.Parent);
    const float ParentAngle = Body.get(Shoulder.Parent).Angle;
    const AngleRange UpperRange = {.Lower = Shoulder.LowerAngle, .Upper = Shoulder.UpperAngle};
    const AngleRange ForearmRange = {.Lower = Elbow.LowerAngle, .Upper = Elbow.UpperAngle};

    // The two bones in the reference pose: shoulder to elbow, elbow to fist.
    const Vec2 UpperBone = Elbow.Anchor - Shoulder.Anchor;
    const Vec2 LowerBone = Def.getPart(Forearm).End - Elbow.Anchor;
    const float UpperLength = UpperBone.getLength();
    const float LowerLength = LowerBone.getLength();
    const Vec2 ToTarget = Target - Pivot;
    const float Distance = std::clamp(ToTarget.getLength(), std::abs(UpperLength - LowerLength) + 1e-6f,
                                      UpperLength + LowerLength);
    // The turn between the bones (law of cosines), then the forearm joint
    // angle that gives it.
    const float CosElbow = std::clamp(
        (UpperLength * UpperLength + LowerLength * LowerLength - Distance * Distance) /
            (2.0f * UpperLength * LowerLength),
        -1.0f, 1.0f);
    const float Turn = Pi - std::acos(CosElbow);
    const float BoneOffset = getDirection(LowerBone) - getDirection(UpperBone);

    std::array<ArmSolution, 2> Solutions;
    for (auto&& [Solution, Bend] : std::views::zip(Solutions, std::array{Preferred, Preferred == ElbowBend::Down
                                                                                    ? ElbowBend::Up
                                                                                    : ElbowBend::Down})) {
        const float Sign = Bend == ElbowBend::Down ? 1.0f : -1.0f;
        const float ForearmAngle = clampJointAngle(Sign * Turn - BoneOffset, ForearmRange);
        // The fist relative to the shoulder with the upper arm at world angle 0.
        const Vec2 Reach = UpperBone + rotate(LowerBone, ForearmAngle);
        // Turn it onto the target; past a shoulder limit, the limit that
        // leaves the fist nearer.
        const float Desired = wrapAngle(getDirection(ToTarget) - getDirection(Reach) - ParentAngle);
        float BestError = std::numeric_limits<float>::max();
        for (const float UpperAngle : {clampJointAngle(Desired, UpperRange), UpperRange.Lower, UpperRange.Upper}) {
            const Vec2 Fist = Pivot + rotate(Reach, ParentAngle + UpperAngle);
            const float Error = (Fist - Target).getLength();
            if (Error >= BestError - 1e-6f) continue;
            BestError = Error;
            Solution.Angles = {.Upper = UpperAngle, .Forearm = ForearmAngle};
            Solution.Fist = Fist;
        }
        Solution.Bend = Bend;
    }
    const float PreferredError = (Solutions[0].Fist - Target).getLength();
    const float OtherError = (Solutions[1].Fist - Target).getLength();
    ArmSolution Result = OtherError < PreferredError - ReachTolerance ? Solutions[1] : Solutions[0];
    Result.Reached = (Result.Fist - Target).getLength() < ReachTolerance;
    return Result;
}

WeaponAim aimWeapon(const rig::RigDef& Def, const PosedBody& Body, const PosedWeapon& Weapon, Vec2 Target) {
    const BodyPart Upper = getJoint(Def, Weapon.Holder).Parent;
    WeaponAim Aim = {.Wrist = Weapon.WristAngle,
                     .Arm = {.Upper = getPosedJointAngle(Def, Body, Upper),
                             .Forearm = getPosedJointAngle(Def, Body, Weapon.Holder)}};
    if ((Target - Weapon.Fist).getLengthSquared() < 1e-12f) return Aim;
    float Left = wrapAngle(getDirection(Target - Weapon.Fist) - getDirection(Weapon.Tip - Weapon.Fist));

    // Each joint takes what it can of the turn, the wrist first.
    const AngleRange Wrist = getWristRange(Def);
    const AngleRange Elbow = getJointRange(Def, Weapon.Holder);
    const AngleRange Shoulder = getJointRange(Def, Upper);
    for (auto&& [Angle, Range] : std::views::zip(std::array{&Aim.Wrist, &Aim.Arm.Forearm, &Aim.Arm.Upper},
                                                 std::array{Wrist, Elbow, Shoulder})) {
        const float Turned = std::clamp(*Angle + Left, Range.Lower, Range.Upper);
        Left -= Turned - *Angle;
        *Angle = Turned;
    }
    return Aim;
}

Vec2 getGripPoint(const PosedWeapon& Weapon, float GripM) {
    const Vec2 Along = Weapon.Tip - Weapon.Fist;
    const float Length = Along.getLength();
    if (Length <= 0.0f) return Weapon.Fist;
    return Weapon.Fist + Along * (std::min(GripM, Length) / Length);
}

float dragJointAngle(float StartAngle, Vec2 Pivot, Vec2 From, Vec2 To, AngleRange Range) {
    const float Turn = wrapAngle(getDirection(To - Pivot) - getDirection(From - Pivot));
    return std::clamp(StartAngle + Turn, Range.Lower, Range.Upper);
}

namespace {

const rig::JointDef* findJoint(const rig::RigDef& Def, BodyPart Child) {
    const auto Found = std::ranges::find(Def.Joints, Child, &rig::JointDef::Child);
    return Found == Def.Joints.end() ? nullptr : &*Found;
}

const rig::JointDef& getJoint(const rig::RigDef& Def, BodyPart Child) {
    const rig::JointDef* Joint = findJoint(Def, Child);
    if (!Joint) throw std::invalid_argument("the part has no joint: it is the root of the rig");
    return *Joint;
}

float wrapAngle(float Angle) { return std::remainder(Angle, FullTurn); }

float getDirection(Vec2 Vector) { return std::atan2(Vector.Y, Vector.X); }

bool isInside(float Angle, AngleRange Range) { return Angle >= Range.Lower && Angle <= Range.Upper; }

} // namespace

} // namespace fighter::editor
