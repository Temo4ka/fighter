#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "editor/arm_roles.hpp"

using namespace fighter;
using namespace fighter::editor;

TEST_CASE("Without swapped arms the roles follow the weapon forearm", "[editor][roles]") {
    // The weapon is in the left forearm (as in data/rigs/humanoid.json).
    CHECK(getRoleJoint(ArmRole::Weapon, ArmSegment::Forearm, BodyPart::ForearmL, false) == BodyPart::ForearmL);
    CHECK(getRoleJoint(ArmRole::Weapon, ArmSegment::Upper, BodyPart::ForearmL, false) == BodyPart::UpperArmL);
    CHECK(getRoleJoint(ArmRole::Other, ArmSegment::Forearm, BodyPart::ForearmL, false) == BodyPart::ForearmR);
    CHECK(getRoleJoint(ArmRole::Other, ArmSegment::Upper, BodyPart::ForearmL, false) == BodyPart::UpperArmR);
    // And for a weapon in the right forearm.
    CHECK(getRoleJoint(ArmRole::Weapon, ArmSegment::Forearm, BodyPart::ForearmR, false) == BodyPart::ForearmR);
    CHECK(getRoleJoint(ArmRole::Other, ArmSegment::Upper, BodyPart::ForearmR, false) == BodyPart::UpperArmL);
}

TEST_CASE("A clip played with swapped arms edits the other side's joints", "[editor][roles]") {
    // The clip's ForearmR moves the ghost's ForearmL, the weapon arm.
    CHECK(getRoleJoint(ArmRole::Weapon, ArmSegment::Forearm, BodyPart::ForearmL, true) == BodyPart::ForearmR);
    CHECK(getRoleJoint(ArmRole::Weapon, ArmSegment::Upper, BodyPart::ForearmL, true) == BodyPart::UpperArmR);
    CHECK(getRoleJoint(ArmRole::Other, ArmSegment::Forearm, BodyPart::ForearmL, true) == BodyPart::ForearmL);
    CHECK(getRoleJoint(ArmRole::Other, ArmSegment::Upper, BodyPart::ForearmL, true) == BodyPart::UpperArmL);
}

TEST_CASE("The role of a joint is the inverse of the role's joint", "[editor][roles]") {
    for (const BodyPart MainHand : {BodyPart::ForearmL, BodyPart::ForearmR}) {
        for (const bool Swapped : {false, true}) {
            for (const ArmRole Role : {ArmRole::Weapon, ArmRole::Other}) {
                for (const ArmSegment Segment : {ArmSegment::Upper, ArmSegment::Forearm}) {
                    const BodyPart Joint = getRoleJoint(Role, Segment, MainHand, Swapped);
                    ArmSegment Found = ArmSegment::Upper;
                    CHECK(getJointRole(Joint, MainHand, Swapped, &Found) == Role);
                    CHECK(Found == Segment);
                }
            }
        }
    }
}

TEST_CASE("Only arm joints have a role", "[editor][roles]") {
    CHECK_FALSE(getJointRole(BodyPart::Torso, BodyPart::ForearmL, false).has_value());
    CHECK_FALSE(getJointRole(BodyPart::ThighL, BodyPart::ForearmL, true).has_value());
}

TEST_CASE("The ghost part of a swapped joint is the same part of the other arm", "[editor][roles]") {
    CHECK(getGhostPart(BodyPart::ForearmR, true) == BodyPart::ForearmL);
    CHECK(getGhostPart(BodyPart::ForearmR, false) == BodyPart::ForearmR);
    CHECK(getGhostPart(BodyPart::Torso, true) == BodyPart::Torso);
}
