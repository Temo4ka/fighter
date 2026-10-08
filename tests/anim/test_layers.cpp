#include <catch2/catch_test_macros.hpp>

#include <bitset>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>

#include "anim/clip.hpp"
#include "anim/layers.hpp"
#include "anim/pose.hpp"

using namespace fighter;
using namespace fighter::anim;

namespace {

const std::filesystem::path PosesDir = std::filesystem::path(FIGHTER_DATA_DIR) / "poses";

constexpr const char* KickJson = R"({
    "duration": 1.0,
    "active": [0.2, 0.4],
    "strikers": ["ShinL", "FootL"],
    "keys": [
        { "t": 0.0, "pose": { "Torso": -8, "ThighL": 25, "ShinL": -30, "FootL": 5 } },
        { "t": 0.5, "pose": { "Torso": 10, "ThighL": 110, "ShinL": -5, "FootL": -15 } }
    ]
})";

std::bitset<BodyPartCount> makeParts(std::initializer_list<BodyPart> Parts) {
    std::bitset<BodyPartCount> Result;
    for (const BodyPart Part : Parts) Result.set(static_cast<size_t>(Part));
    return Result;
}

} // namespace

TEST_CASE("Layers: the legs are the thighs, shins and feet", "[anim][layers]") {
    CHECK(getLegJoints() == makeParts({BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR,
                                       BodyPart::ShinR, BodyPart::FootR}));
    CHECK(getLayer(BodyPart::FootR) == Layer::Legs);
    CHECK(getLayer(BodyPart::Pelvis) == Layer::Upper);
    CHECK(getLayer(BodyPart::ForearmL) == Layer::Upper);
}

TEST_CASE("Layers: which clips of data/poses use the legs", "[anim][layers]") {
    // The classification the layered walk plays by (docs/TUNING.md): the
    // punches, the upper blocks and the reactions leave the legs alone.
    for (const char* Name : {"walk", "kick", "crouch", "crouch_walk", "block_low", "stance"}) {
        INFO(Name);
        CHECK(usesLegs(loadClip(PosesDir / (std::string(Name) + ".json"))));
    }
    for (const char* Name : {"jab", "jab_close", "heavy_punch", "heavy_punch_close", "sword_slash", "hammer_smash",
                             "block_high", "block_mid", "flinch", "stagger", "knockback"}) {
        INFO(Name);
        CHECK_FALSE(usesLegs(loadClip(PosesDir / (std::string(Name) + ".json"))));
    }
}

TEST_CASE("Layers: selectJoints and joinLayers keep each layer's joints", "[anim][layers]") {
    Pose Upper;
    Upper.setAngle(BodyPart::Torso, 0.1f);
    Upper.setAngle(BodyPart::ThighL, 0.2f);
    Pose Legs;
    Legs.setAngle(BodyPart::ThighL, 0.5f);
    Legs.setAngle(BodyPart::Head, 0.7f);

    const Pose Selected = selectJoints(Upper, makeParts({BodyPart::Torso}));
    CHECK(Selected.hasJoint(BodyPart::Torso));
    CHECK_FALSE(Selected.hasJoint(BodyPart::ThighL));

    const Pose Joined = joinLayers(Upper, Legs);
    CHECK(Joined.getAngle(BodyPart::Torso) == 0.1f);
    CHECK(Joined.getAngle(BodyPart::ThighL) == 0.5f);
    // The head of the leg layer and the thigh of the upper one are dropped.
    CHECK_FALSE(Joined.hasJoint(BodyPart::Head));
}

TEST_CASE("Layers: mirrorLegs swaps the legs and leaves the upper body", "[anim][layers]") {
    Pose Source;
    Source.setAngle(BodyPart::ThighL, 0.4f);
    Source.setAngle(BodyPart::FootR, -0.2f);
    Source.setAngle(BodyPart::UpperArmL, 0.9f);
    const Pose Mirrored = mirrorLegs(Source);
    CHECK(Mirrored.getAngle(BodyPart::ThighR) == 0.4f);
    CHECK_FALSE(Mirrored.hasJoint(BodyPart::ThighL));
    CHECK(Mirrored.getAngle(BodyPart::FootL) == -0.2f);
    CHECK_FALSE(Mirrored.hasJoint(BodyPart::FootR));
    CHECK(Mirrored.getAngle(BodyPart::UpperArmL) == 0.9f);
    CHECK_FALSE(Mirrored.hasJoint(BodyPart::UpperArmR));
    // Twice is the original.
    const Pose Back = mirrorLegs(Mirrored);
    CHECK(Back.Mask == Source.Mask);
    CHECK(Back.Angles == Source.Angles);

    CHECK(getMirroredLegPart(BodyPart::ShinL) == BodyPart::ShinR);
    CHECK(getMirroredLegPart(BodyPart::FootR) == BodyPart::FootL);
    CHECK(getMirroredLegPart(BodyPart::ForearmL) == BodyPart::ForearmL);
    CHECK(mirrorLegParts(makeParts({BodyPart::ShinL, BodyPart::ForearmL})) ==
          makeParts({BodyPart::ShinR, BodyPart::ForearmL}));
}

TEST_CASE("Layers: mirrorClipLegs kicks with the other leg at the same time", "[anim][layers]") {
    const Clip Kick = parseClip(KickJson, "kick");
    const Clip Mirrored = mirrorClipLegs(Kick);
    CHECK(Mirrored.Name == "kick" + std::string(MirroredSuffix));
    CHECK(Mirrored.isStriker(BodyPart::ShinR));
    CHECK(Mirrored.isStriker(BodyPart::FootR));
    CHECK_FALSE(Mirrored.isStriker(BodyPart::FootL));
    CHECK(Mirrored.ActiveBeginSec == Kick.ActiveBeginSec);
    CHECK(Mirrored.ActiveEndSec == Kick.ActiveEndSec);
    CHECK(Mirrored.DurationSec == Kick.DurationSec);
    for (const float Time : {0.0f, 0.25f, 0.5f, 0.9f}) {
        const Pose Authored = sampleClip(Kick, Time);
        const Pose Played = sampleClip(Mirrored, Time);
        CHECK(Played.getAngle(BodyPart::ThighR) == Authored.getAngle(BodyPart::ThighL));
        CHECK(Played.getAngle(BodyPart::Torso) == Authored.getAngle(BodyPart::Torso));
        CHECK_FALSE(Played.hasJoint(BodyPart::ThighL));
    }
}

TEST_CASE("Layers: the mirrored and other-hand clips keep the pelvis track", "[anim][layers][pelvis]") {
    const Clip Lunge = parseClip(R"({
        "duration": 0.6,
        "active": [0.1, 0.3],
        "strikers": ["FootL", "ForearmR"],
        "pelvisX": [ { "t": 0.0, "x": 0.0 }, { "t": 0.3, "x": 0.25 }, { "t": 0.6, "x": 0.1 } ],
        "keys": [ { "t": 0.0, "pose": { "ThighL": 20, "UpperArmR": 40 } } ]
    })", "lunge");
    // The track is forward along the facing: swapping the legs or the arms
    // does not change where forward is.
    for (const Clip& Played : {mirrorClipLegs(Lunge), mirrorClipArms(Lunge), mirrorClipArms(mirrorClipLegs(Lunge))}) {
        for (const float Time : {0.0f, 0.15f, 0.3f, 0.45f, 0.6f}) {
            CHECK(samplePelvisOffset(Played, Time) == samplePelvisOffset(Lunge, Time));
        }
    }
}

TEST_CASE("Layers: mirrorClipArms strikes with the other arm", "[anim][layers]") {
    const Clip Slash = parseClip(R"({
        "duration": 0.5,
        "active": [0.1, 0.2],
        "strikers": ["ForearmR"],
        "keys": [
            { "t": 0.0, "pose": { "Torso": 5, "UpperArmR": 40, "ForearmR": 90 } },
            { "t": 0.2, "pose": { "Torso": 5, "UpperArmR": 120, "ForearmR": 10 } }
        ]
    })", "slash");
    CHECK(usesArms(Slash));
    CHECK_FALSE(usesArms(parseClip(KickJson, "kick")));
    CHECK(getMirroredArmPart(BodyPart::ForearmR) == BodyPart::ForearmL);
    CHECK(getMirroredArmPart(BodyPart::UpperArmL) == BodyPart::UpperArmR);
    CHECK(getMirroredArmPart(BodyPart::Head) == BodyPart::Head);
    CHECK(mirrorArmParts(makeParts({BodyPart::ForearmR, BodyPart::Head})) ==
          makeParts({BodyPart::ForearmL, BodyPart::Head}));

    const Clip Left = mirrorClipArms(Slash);
    CHECK(Left.Name == "slash (other hand)");
    CHECK(Left.isStriker(BodyPart::ForearmL));
    CHECK_FALSE(Left.isStriker(BodyPart::ForearmR));
    CHECK(Left.ActiveBeginSec == Slash.ActiveBeginSec);
    const Pose Swapped = mirrorArms(Slash.Keys.back().Target);
    CHECK(Swapped.hasJoint(BodyPart::UpperArmL));
    CHECK_FALSE(Swapped.hasJoint(BodyPart::UpperArmR));
    CHECK(Swapped.getAngle(BodyPart::UpperArmL) == Slash.Keys.back().Target.getAngle(BodyPart::UpperArmR));
    CHECK(Swapped.getAngle(BodyPart::Torso) == Slash.Keys.back().Target.getAngle(BodyPart::Torso));
}

TEST_CASE("joinLayers: the wrist comes with the upper layer, selectJoints drops it", "[anim][layers]") {
    Pose Upper;
    Upper.setAngle(BodyPart::ForearmL, 0.5f);
    Upper.setWeaponAngle(0.7f);
    Pose Legs;
    Legs.setAngle(BodyPart::ThighL, 0.3f);
    Legs.setWeaponAngle(-1.0f);
    const Pose Joined = joinLayers(Upper, Legs);
    REQUIRE(Joined.HasWeapon);
    CHECK(Joined.WeaponAngle == 0.7f);
    CHECK_FALSE(selectJoints(Upper, getLegJoints()).HasWeapon);
    // The other hand and the mirrored legs keep it: it is either hand's.
    CHECK(mirrorArms(Upper).WeaponAngle == 0.7f);
    CHECK(mirrorLegs(Upper).WeaponAngle == 0.7f);
}

TEST_CASE("joinLayers and mirrorArms: the other wrist goes with the upper layer, by role", "[anim][layers]") {
    Pose Upper;
    Upper.setWeaponAngle(0.7f);
    Upper.setWeaponOffAngle(-0.4f);
    const Pose Joined = joinLayers(Upper, Pose{});
    REQUIRE(Joined.HasWeaponOff);
    CHECK(Joined.WeaponOffAngle == -0.4f);
    CHECK_FALSE(selectJoints(Upper, getLegJoints()).HasWeaponOff);
    // Swapping the arms keeps the roles: the weapon arm swaps with them.
    const Pose Mirrored = mirrorArms(Upper);
    CHECK(Mirrored.WeaponAngle == 0.7f);
    CHECK(Mirrored.WeaponOffAngle == -0.4f);
}

TEST_CASE("getWeaponArm: the main hand unless the clip strikes only with the other arm", "[anim][layers]") {
    const Clip Stance;
    CHECK(getWeaponArm(Stance, BodyPart::ForearmL) == BodyPart::ForearmL);
    Clip Cut;
    Cut.Strikers = makeParts({BodyPart::ForearmR});
    CHECK(getWeaponArm(Cut, BodyPart::ForearmL) == BodyPart::ForearmR);
    CHECK(getWeaponArm(Cut, BodyPart::ForearmR) == BodyPart::ForearmR);
    // Played with the arms swapped, the weapon arm swaps too.
    CHECK(getWeaponArm(mirrorClipArms(Cut), BodyPart::ForearmL) == BodyPart::ForearmL);
    Clip Elbow;
    Elbow.Strikers = makeParts({BodyPart::UpperArmR});
    CHECK(getWeaponArm(Elbow, BodyPart::ForearmL) == BodyPart::ForearmR);
    Clip Both;
    Both.Strikers = makeParts({BodyPart::ForearmL, BodyPart::ForearmR});
    CHECK(getWeaponArm(Both, BodyPart::ForearmL) == BodyPart::ForearmL);
    Clip Kick;
    Kick.Strikers = makeParts({BodyPart::ShinL});
    CHECK(getWeaponArm(Kick, BodyPart::ForearmL) == BodyPart::ForearmL);
}

TEST_CASE("swapWrists: exchanges the two wrists and whether they are set", "[anim][layers]") {
    Pose Source;
    Source.setWeaponAngle(0.3f);
    const Pose Swapped = swapWrists(Source);
    CHECK_FALSE(Swapped.HasWeapon);
    REQUIRE(Swapped.HasWeaponOff);
    CHECK(Swapped.WeaponOffAngle == 0.3f);
    CHECK(swapWrists(Swapped).WeaponAngle == 0.3f);
}
