#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "anim/clip.hpp"
#include "anim/layers.hpp"
#include "editor/clip_history.hpp"
#include "editor/pose_fk.hpp"
#include "editor/clip_writer.hpp"
#include "editor/pose_ops.hpp"
#include "rig/rig_def.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

rig::RigDef loadRig() { return rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json"); }

/// A clip for the right hand: arms and legs keyed, the wrist on the first
/// two keys, three keys in all.
anim::Clip makeClip() {
    return anim::parseClip(R"({
      "duration": 0.6, "strikers": ["ForearmR"],
      "keys": [
        { "t": 0.0, "pose": { "UpperArmL": 10, "ForearmL": 20, "UpperArmR": 30, "ForearmR": 40,
                              "ThighL": 5, "ShinL": 6, "FootL": 7, "ThighR": -5, "ShinR": 8, "FootR": 9,
                              "Torso": 3, "Weapon": 15 } },
        { "t": 0.3, "pose": { "UpperArmL": 50, "ForearmL": 60, "UpperArmR": 70, "ForearmR": 80,
                              "ThighL": 1, "ShinL": 2, "FootL": 3, "ThighR": 4, "ShinR": 5, "FootR": 6,
                              "Torso": 4, "Weapon": -20 } },
        { "t": 0.5, "pose": { "UpperArmL": 11, "ForearmL": 12, "UpperArmR": 13, "ForearmR": 14,
                              "ThighL": 0, "ShinL": 0, "FootL": 0, "ThighR": 0, "ShinR": 0, "FootR": 0,
                              "Torso": 5, "Weapon": 0 } }
      ] })",
                           "ops");
}

float getDegrees(const anim::Clip& Source, size_t Key, BodyPart Part) {
    return Source.Keys[Key].Target.getAngle(Part) * 180.0f / 3.14159265f;
}

} // namespace

TEST_CASE("Paste writes the copied pose and keeps the time of the key", "[editor][pose_ops]") {
    const rig::RigDef Def = loadRig();
    anim::Clip Edited = makeClip();
    const anim::Pose Copied = copyKeyPose(Edited, 1);
    const size_t Written = pasteKeyPose(Edited, 2, Copied, Def);
    CHECK(Written == 12);
    CHECK(Edited.Keys[2].TimeSec == Approx(0.5f));
    CHECK(getDegrees(Edited, 2, BodyPart::ForearmR) == Approx(80.0f).margin(0.01));
    CHECK(getDegrees(Edited, 2, BodyPart::Torso) == Approx(4.0f).margin(0.01));
    CHECK(Edited.Keys[2].Target.WeaponAngle == Approx(Edited.Keys[1].Target.WeaponAngle));
    CHECK(findClipProblem(Edited).empty());
}

TEST_CASE("Paste skips joints the clip does not key and the copy lacks", "[editor][pose_ops]") {
    const rig::RigDef Def = loadRig();
    anim::Clip Edited = makeClip();
    anim::Pose Copied = copyKeyPose(Edited, 1);
    Copied.Mask.reset(static_cast<size_t>(BodyPart::Torso));
    Copied.HasWeapon = false;
    const float TorsoBefore = Edited.Keys[0].Target.getAngle(BodyPart::Torso);
    const float WristBefore = Edited.Keys[0].Target.WeaponAngle;
    CHECK(pasteKeyPose(Edited, 0, Copied, Def) == 10);
    CHECK(Edited.Keys[0].Target.getAngle(BodyPart::Torso) == TorsoBefore);
    CHECK(Edited.Keys[0].Target.WeaponAngle == WristBefore);

    // A joint the copy has but the clip does not key stays unkeyed.
    anim::Clip Plain = anim::parseClip(R"({ "duration": 0.5, "keys": [ { "t": 0, "pose": { "Torso": 1 } } ] })", "plain");
    CHECK(pasteKeyPose(Plain, 0, copyKeyPose(makeClip(), 0), Def) == 1);
    CHECK_FALSE(Plain.Keys[0].Target.hasJoint(BodyPart::ForearmL));
    CHECK_FALSE(Plain.Keys[0].Target.HasWeapon);
}

TEST_CASE("Paste keeps the angles within the limits", "[editor][pose_ops]") {
    const rig::RigDef Def = loadRig();
    anim::Clip Edited = makeClip();
    anim::Pose Copied = copyKeyPose(Edited, 0);
    Copied.setAngle(BodyPart::ForearmL, 10.0f);
    Copied.setWeaponAngle(10.0f);
    pasteKeyPose(Edited, 1, Copied, Def);
    CHECK(Edited.Keys[1].Target.getAngle(BodyPart::ForearmL) <= getJointRange(Def, BodyPart::ForearmL).Upper + 1e-5f);
    CHECK(Edited.Keys[1].Target.WeaponAngle <= getWristRange(Def).Upper + 1e-5f);
    CHECK(pasteKeyPose(Edited, 9, Copied, Def) == 0);
    CHECK(copyKeyPose(Edited, 9).Mask.none());
}

TEST_CASE("Mirroring a key swaps the arms and the legs and keeps the wrist", "[editor][pose_ops]") {
    anim::Clip Edited = makeClip();
    CHECK(mirrorKey(Edited, 0) == 0);
    CHECK(getDegrees(Edited, 0, BodyPart::UpperArmL) == Approx(30.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::UpperArmR) == Approx(10.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::ForearmL) == Approx(40.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::ForearmR) == Approx(20.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::ThighL) == Approx(-5.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::ThighR) == Approx(5.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::FootL) == Approx(9.0f).margin(0.01));
    CHECK(getDegrees(Edited, 0, BodyPart::Torso) == Approx(3.0f).margin(0.01));
    CHECK(Edited.Keys[0].Target.WeaponAngle == Approx(makeClip().Keys[0].Target.WeaponAngle));
    // Other keys and the strikers are not touched.
    CHECK(getDegrees(Edited, 1, BodyPart::ForearmR) == Approx(80.0f).margin(0.01));
    CHECK(Edited.isStriker(BodyPart::ForearmR));
    CHECK(findClipProblem(Edited).empty());
}

TEST_CASE("Mirroring a key twice is the identity", "[editor][pose_ops]") {
    anim::Clip Edited = makeClip();
    mirrorKey(Edited, 1);
    CHECK_FALSE(isSameClip(Edited, makeClip()));
    mirrorKey(Edited, 1);
    CHECK(isSameClip(Edited, makeClip()));
}

TEST_CASE("Mirroring a key leaves a pair that only one side keys", "[editor][pose_ops]") {
    anim::Clip Edited = anim::parseClip(R"({ "duration": 0.5, "keys": [
        { "t": 0, "pose": { "ForearmL": 10, "UpperArmL": 20, "UpperArmR": 30 } } ] })", "one");
    CHECK(mirrorKey(Edited, 0) == 1);
    CHECK(getDegrees(Edited, 0, BodyPart::ForearmL) == Approx(10.0f).margin(0.01));
    CHECK_FALSE(Edited.Keys[0].Target.hasJoint(BodyPart::ForearmR));
    CHECK(getDegrees(Edited, 0, BodyPart::UpperArmL) == Approx(30.0f).margin(0.01));
    CHECK(findClipProblem(Edited).empty());
    CHECK(mirrorKey(Edited, 5) == 0);
}

TEST_CASE("Mirroring a clip swaps the strikers too and twice is the identity", "[editor][pose_ops]") {
    anim::Clip Edited = makeClip();
    mirrorClip(Edited);
    CHECK(Edited.isStriker(BodyPart::ForearmL));
    CHECK_FALSE(Edited.isStriker(BodyPart::ForearmR));
    for (size_t Key = 0; Key < Edited.Keys.size(); ++Key) {
        CHECK(getDegrees(Edited, Key, BodyPart::ForearmL) == Approx(getDegrees(makeClip(), Key, BodyPart::ForearmR)).margin(0.01));
    }
    CHECK(findClipProblem(Edited).empty());
    mirrorClip(Edited);
    CHECK(isSameClip(Edited, makeClip()));

    // The clip that the game plays for the other hand has the arms of the mirror.
    const anim::Clip Played = anim::mirrorClipArms(makeClip());
    anim::Clip OnlyArms = makeClip();
    mirrorClip(OnlyArms);
    CHECK(OnlyArms.Strikers == Played.Strikers);
    CHECK(OnlyArms.Keys[1].Target.getAngle(BodyPart::ForearmL) == Played.Keys[1].Target.getAngle(BodyPart::ForearmL));
}

TEST_CASE("Onion keys are the nearest neighbours on both sides", "[editor][pose_ops]") {
    const anim::Clip Edited = makeClip();
    OnionKeys Onion = findOnionKeys(Edited, 1, 1);
    CHECK(Onion.Previous == std::vector<size_t>{0});
    CHECK(Onion.Next == std::vector<size_t>{2});

    Onion = findOnionKeys(Edited, 0, 2);
    CHECK(Onion.Previous.empty());
    CHECK(Onion.Next == std::vector<size_t>{1, 2});

    Onion = findOnionKeys(Edited, 2, 2);
    CHECK(Onion.Previous == std::vector<size_t>{1, 0});
    CHECK(Onion.Next.empty());

    CHECK(findOnionKeys(Edited, 1, 0).Previous.empty());
    CHECK(findOnionKeys(Edited, 7, 2).Next.empty());
}

TEST_CASE("Onion keys go round a looping clip without repeating", "[editor][pose_ops]") {
    anim::Clip Edited = makeClip();
    Edited.Loop = true;
    OnionKeys Onion = findOnionKeys(Edited, 0, 1);
    CHECK(Onion.Previous == std::vector<size_t>{2});
    CHECK(Onion.Next == std::vector<size_t>{1});

    // Three keys, two on each side: the key behind is the one ahead.
    Onion = findOnionKeys(Edited, 0, 2);
    CHECK(Onion.Previous == std::vector<size_t>{2, 1});
    CHECK(Onion.Next.empty());

    Edited.Keys.resize(1);
    Onion = findOnionKeys(Edited, 0, 2);
    CHECK(Onion.Previous.empty());
    CHECK(Onion.Next.empty());
}
