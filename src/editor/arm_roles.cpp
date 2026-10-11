#include "editor/arm_roles.hpp"

#include "anim/layers.hpp"

namespace fighter::editor {
namespace {

bool isArmJoint(BodyPart Part);
bool isLeftArm(BodyPart Part);
BodyPart getArmJoint(bool Left, ArmSegment Segment);

} // namespace

BodyPart getRoleJoint(ArmRole Role, ArmSegment Segment, BodyPart MainHand, bool PlayedOtherHand) {
    // The side of the ghost that has the role, then the clip joint that moves it.
    const bool GhostLeft = (Role == ArmRole::Weapon) == isLeftArm(MainHand);
    const bool JointLeft = PlayedOtherHand ? !GhostLeft : GhostLeft;
    return getArmJoint(JointLeft, Segment);
}

std::optional<ArmRole> getJointRole(BodyPart Joint, BodyPart MainHand, bool PlayedOtherHand, ArmSegment* Segment) {
    if (!isArmJoint(Joint)) return std::nullopt;
    if (Segment) *Segment = (Joint == BodyPart::ForearmL || Joint == BodyPart::ForearmR) ? ArmSegment::Forearm
                                                                                         : ArmSegment::Upper;
    const bool GhostLeft = isLeftArm(getGhostPart(Joint, PlayedOtherHand));
    return GhostLeft == isLeftArm(MainHand) ? ArmRole::Weapon : ArmRole::Other;
}

BodyPart getGhostPart(BodyPart Joint, bool PlayedOtherHand) {
    return PlayedOtherHand ? anim::getMirroredArmPart(Joint) : Joint;
}

namespace {

bool isArmJoint(BodyPart Part) {
    return Part == BodyPart::UpperArmL || Part == BodyPart::ForearmL || Part == BodyPart::UpperArmR ||
           Part == BodyPart::ForearmR;
}

bool isLeftArm(BodyPart Part) { return Part == BodyPart::UpperArmL || Part == BodyPart::ForearmL; }

BodyPart getArmJoint(bool Left, ArmSegment Segment) {
    if (Segment == ArmSegment::Upper) return Left ? BodyPart::UpperArmL : BodyPart::UpperArmR;
    return Left ? BodyPart::ForearmL : BodyPart::ForearmR;
}

} // namespace

} // namespace fighter::editor
