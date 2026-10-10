#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <numbers>
#include <string>

#include "anim/clip.hpp"
#include "editor/arm_ik.hpp"
#include "editor/clip_edit.hpp"
#include "editor/clip_writer.hpp"
#include "editor/ghost.hpp"
#include "editor/pose_drag.hpp"
#include "editor/pose_fk.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

anim::Clip loadPose(const char* Name) { return anim::loadClip(DataDir / "poses" / (std::string(Name) + ".json")); }

/// The ghost of \p Edited at its key \p KeyIndex, posed.
PosedBody poseKey(const GhostContext& Context, const anim::Clip& Edited, size_t KeyIndex) {
    const anim::Pose Ghost =
        composeGhostPose(Context.Stance, Edited, Edited.Keys[KeyIndex].TimeSec, Context.WeaponHand);
    return poseBody(Context.Rig, Ghost, Context.Held);
}

const PosedWeapon& getWeapon(const GhostContext& Context, const PosedBody& Body) {
    return *std::ranges::find(Body.Weapons, *Context.WeaponHand, &PosedWeapon::Holder);
}

} // namespace

TEST_CASE("Only the joints of the upper body are dragged", "[editor][drag]") {
    const rig::RigDef Def = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");
    for (const BodyPart Part : {BodyPart::Torso, BodyPart::Head, BodyPart::UpperArmL, BodyPart::ForearmR}) {
        CHECK(isDraggablePart(Def, Part));
    }
    for (const BodyPart Part : {BodyPart::Pelvis, BodyPart::ThighL, BodyPart::ShinR, BodyPart::FootL}) {
        CHECK_FALSE(isDraggablePart(Def, Part));
    }
}

TEST_CASE("The distance to a part is to its surface", "[editor][drag]") {
    const rig::RigDef Def = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");
    anim::Pose Pose;
    Pose.setAngle(BodyPart::UpperArmL, 90.0f * RadiansPerDegree);   // forward and level
    const PosedBody Body = poseBody(Def, Pose);
    // The head: a circle of 0.11 m about (0.01, 1.65).
    CHECK(getDistanceToPart(Def.getPart(BodyPart::Head), Body.get(BodyPart::Head), {0.01f, 1.65f}) ==
          Approx(-0.11f));
    CHECK(getDistanceToPart(Def.getPart(BodyPart::Head), Body.get(BodyPart::Head), {0.31f, 1.65f}) ==
          Approx(0.19f));
    // The raised upper arm runs forward from the shoulder at 1.42 m.
    const PosedPart& Upper = Body.get(BodyPart::UpperArmL);
    CHECK(getDistanceToPart(Def.getPart(BodyPart::UpperArmL), Upper, Upper.Position) == Approx(-0.05f));
    CHECK(getDistanceToPart(Def.getPart(BodyPart::UpperArmL), Upper, Upper.Position + Vec2{0.0f, 0.15f}) ==
          Approx(0.10f).margin(1e-4));
    // The pelvis box, 0.11 x 0.08 half extents.
    const PosedPart& Pelvis = Body.get(BodyPart::Pelvis);
    CHECK(getDistanceToPart(Def.getPart(BodyPart::Pelvis), Pelvis, Pelvis.Position + Vec2{0.2f, 0.0f}) ==
          Approx(0.09f).margin(1e-4));
}

TEST_CASE("Dragging the head keys it in the clip without changing the other keys", "[editor][drag]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "short_sword");
    anim::Clip Cut = loadPose("sword_cut");
    setJointKeyed(Cut, BodyPart::Head, false, 0.0f);
    const anim::Pose Ghost = composeGhostPose(Context.Stance, Cut, Cut.Keys[1].TimeSec, Context.WeaponHand);
    const PosedBody Body = poseBody(Context.Rig, Ghost, Context.Held);
    const Vec2 Pivot = getJointPivot(Context.Rig, Body, BodyPart::Head);
    // A tenth of a turn counter-clockwise about the neck.
    const Vec2 From = Pivot + Vec2{0.0f, 0.2f};
    const Vec2 To = Pivot + rotate({0.0f, 0.2f}, 0.1f);
    const GhostEdit Edit = rotateGhostJoint(Context, Ghost, BodyPart::Head, From, To, ElbowBend::Down);
    CHECK(Edit.Joints.count() == 1);
    CHECK(Edit.After.getAngle(BodyPart::Head) == Approx(Ghost.getAngle(BodyPart::Head) + 0.1f));

    applyGhostEdit(Edit, Cut, 1, isPlayedOtherHand(Cut, Context.WeaponHand));
    CHECK(isJointKeyed(Cut, BodyPart::Head));
    CHECK(Cut.Keys[1].Target.getAngle(BodyPart::Head) == Approx(Ghost.getAngle(BodyPart::Head) + 0.1f));
    CHECK(Cut.Keys[0].Target.getAngle(BodyPart::Head) == Approx(Ghost.getAngle(BodyPart::Head)));
    CHECK(findClipProblem(Cut) == "");
}

TEST_CASE("A fist moved on the ghost of a swapped clip is written to the clip's own joints", "[editor][drag]") {
    // The sword is in the left forearm; sword_cut is authored for the right.
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "short_sword");
    anim::Clip Cut = loadPose("sword_cut");
    const bool Swapped = isPlayedOtherHand(Cut, Context.WeaponHand);
    REQUIRE(Swapped);
    const size_t KeyIndex = 1;
    const anim::Pose Ghost = composeGhostPose(Context.Stance, Cut, Cut.Keys[KeyIndex].TimeSec, Context.WeaponHand);
    const PosedBody Body = poseBody(Context.Rig, Ghost, Context.Held);
    const BodyPart Hand = *Context.WeaponHand;
    const Vec2 Target = getFistPoint(Context.Rig, Body, Hand) + Vec2{-0.04f, 0.05f};
    const anim::Keyframe Old = Cut.Keys[KeyIndex];

    const GhostEdit Edit = moveGhostFist(Context, Ghost, Hand, Target, ElbowBend::Down);
    CHECK(Edit.Joints.test(static_cast<size_t>(BodyPart::ForearmL)));
    CHECK(Edit.Joints.test(static_cast<size_t>(BodyPart::UpperArmL)));
    applyGhostEdit(Edit, Cut, KeyIndex, Swapped);

    // The file's right arm moved, its left arm did not.
    CHECK(Cut.Keys[KeyIndex].Target.getAngle(BodyPart::ForearmR) == Approx(Edit.After.getAngle(BodyPart::ForearmL)));
    CHECK(Cut.Keys[KeyIndex].Target.getAngle(BodyPart::UpperArmR) == Approx(Edit.After.getAngle(BodyPart::UpperArmL)));
    CHECK(Cut.Keys[KeyIndex].Target.getAngle(BodyPart::ForearmL) == Approx(Old.Target.getAngle(BodyPart::ForearmL)));
    // The ghost of the edited clip has its fist on the target.
    const PosedBody After = poseKey(Context, Cut, KeyIndex);
    CHECK((getFistPoint(Context.Rig, After, Hand) - Target).getLength() < 2e-3f);
}

TEST_CASE("Dragging the tip points the blade and keeps the fist within the wrist", "[editor][drag]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "short_sword");
    anim::Clip Cut = loadPose("sword_cut");
    const anim::Pose Ghost = composeGhostPose(Context.Stance, Cut, Cut.Keys[1].TimeSec, Context.WeaponHand);
    const PosedWeapon Before = getWeapon(Context, poseBody(Context.Rig, Ghost, Context.Held));
    const Vec2 Target = Before.Fist + rotate(Before.Tip - Before.Fist, 20.0f * RadiansPerDegree);
    const GhostEdit Edit = aimGhostWeapon(Context, Ghost, Target, ElbowBend::Down);
    CHECK(Edit.Wrist);
    applyGhostEdit(Edit, Cut, 1, isPlayedOtherHand(Cut, Context.WeaponHand));
    CHECK(Cut.Keys[1].Target.HasWeapon);

    const PosedWeapon After = getWeapon(Context, poseKey(Context, Cut, 1));
    CHECK((After.Fist - Before.Fist).getLength() < 1e-4f);
    CHECK(cross((After.Tip - After.Fist).getNormalized(), (Target - Before.Fist).getNormalized()) ==
          Approx(0.0f).margin(1e-4));
}

TEST_CASE("The other hand holds the handle of a two-handed weapon", "[editor][drag]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "greatsword");
    REQUIRE(Context.GripHand.has_value());
    anim::Clip Cut = loadPose("greatsword_cut");
    const bool Swapped = isPlayedOtherHand(Cut, Context.WeaponHand);
    const size_t KeyIndex = 1;
    const anim::Pose Ghost = composeGhostPose(Context.Stance, Cut, Cut.Keys[KeyIndex].TimeSec, Context.WeaponHand);
    const PosedBody Body = poseBody(Context.Rig, Ghost, Context.Held);
    auto checkGrip = [&](const GhostEdit& Edit) {
        CHECK(Edit.Joints.test(static_cast<size_t>(*Context.GripHand)));
        anim::Clip Edited = Cut;
        applyGhostEdit(Edit, Edited, KeyIndex, Swapped);
        const PosedBody After = poseKey(Context, Edited, KeyIndex);
        const Vec2 Handle = getGripPoint(getWeapon(Context, After), Context.GripM);
        CHECK((getFistPoint(Context.Rig, After, *Context.GripHand) - Handle).getLength() < 2e-3f);
    };

    SECTION("when the main fist moves") {
        const Vec2 Target = getFistPoint(Context.Rig, Body, *Context.WeaponHand) + Vec2{0.05f, 0.03f};
        checkGrip(moveGhostFist(Context, Ghost, *Context.WeaponHand, Target, ElbowBend::Down));
    }
    SECTION("when the blade turns") {
        const PosedWeapon& Weapon = getWeapon(Context, Body);
        const Vec2 Target = Weapon.Fist + rotate(Weapon.Tip - Weapon.Fist, -15.0f * RadiansPerDegree);
        checkGrip(aimGhostWeapon(Context, Ghost, Target, ElbowBend::Down));
    }
    SECTION("when the torso leans") {
        const Vec2 Pivot = getJointPivot(Context.Rig, Body, BodyPart::Torso);
        const Vec2 From = Pivot + Vec2{0.0f, 0.3f};
        checkGrip(rotateGhostJoint(Context, Ghost, BodyPart::Torso, From, Pivot + rotate({0.0f, 0.3f}, -0.1f),
                                   ElbowBend::Down));
    }
}
