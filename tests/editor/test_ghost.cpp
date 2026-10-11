#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "anim/clip.hpp"
#include "anim/layers.hpp"
#include "editor/ghost.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

anim::Clip loadPose(const char* Name) { return anim::loadClip(DataDir / "poses" / (std::string(Name) + ".json")); }

} // namespace

TEST_CASE("The ghost context of a sword has the sword in the main hand and its stance", "[editor][ghost]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "short_sword");
    REQUIRE(Context.Held.size() == 1);
    CHECK(Context.Held.front().Part == Context.Rig.Weapon.Part);
    CHECK(Context.WeaponHand == Context.Rig.Weapon.Part);
    CHECK(Context.Stance.Name == "stance_sword");
    CHECK(Context.ItemName == "Short sword");
}

TEST_CASE("The ghost context without an item is bare hands in the general stance", "[editor][ghost]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    CHECK(Context.Held.empty());
    CHECK_FALSE(Context.WeaponHand.has_value());
    CHECK(Context.Stance.Name == "stance");
}

TEST_CASE("An unknown item is an error that names it", "[editor][ghost]") {
    try {
        loadGhostContext(DataDir.parent_path(), "no_such_item");
        FAIL("expected an exception");
    } catch (const std::runtime_error& Error) {
        CHECK(std::string(Error.what()).find("no_such_item") != std::string::npos);
    }
}

TEST_CASE("A clip for the other hand plays with the arms swapped", "[editor][ghost]") {
    const anim::Clip Cut = loadPose("sword_cut");   // strikers: ForearmR
    CHECK(Cut.isStriker(BodyPart::ForearmR));
    // The rig holds weapons in the left forearm.
    CHECK(isPlayedOtherHand(Cut, BodyPart::ForearmL));
    CHECK_FALSE(isPlayedOtherHand(Cut, BodyPart::ForearmR));
    CHECK_FALSE(isPlayedOtherHand(Cut, std::nullopt));

    const anim::Clip Played = getPlayedClip(Cut, BodyPart::ForearmL);
    CHECK(Played.isStriker(BodyPart::ForearmL));
    CHECK(Played.Keys[1].Target.getAngle(BodyPart::ForearmL) ==
          Approx(Cut.Keys[1].Target.getAngle(BodyPart::ForearmR)));
    CHECK_FALSE(Played.Keys[1].Target.hasJoint(BodyPart::ForearmR));

    // Authored for the right hand and played in the right: as it is.
    const anim::Clip Same = getPlayedClip(Cut, BodyPart::ForearmR);
    CHECK(Same.Keys[1].Target.getAngle(BodyPart::ForearmR) == Approx(Cut.Keys[1].Target.getAngle(BodyPart::ForearmR)));
}

TEST_CASE("The ghost is the stance with the clip over it", "[editor][ghost]") {
    const anim::Clip Stance = loadPose("stance_sword");
    const anim::Clip Jab = loadPose("jab");   // strikers: ForearmL, keys Torso and the left arm
    const anim::Pose Ghost = composeGhostPose(Stance, Jab, 0.095f, BodyPart::ForearmL);

    // The clip's joints are the clip's, sampled as the game samples them.
    const anim::Pose Sampled = anim::sampleClip(Jab, 0.095f);
    CHECK(Ghost.getAngle(BodyPart::ForearmL) == Approx(Sampled.getAngle(BodyPart::ForearmL)));
    CHECK(Ghost.getAngle(BodyPart::Torso) == Approx(Sampled.getAngle(BodyPart::Torso)));
    // The rest is the stance: the right arm, the legs, the wrist.
    const anim::Pose Base = anim::sampleClip(Stance, 0.0f);
    CHECK(Ghost.getAngle(BodyPart::ForearmR) == Approx(Base.getAngle(BodyPart::ForearmR)));
    CHECK(Ghost.getAngle(BodyPart::ThighL) == Approx(Base.getAngle(BodyPart::ThighL)));
    CHECK(Ghost.HasWeapon);
    CHECK(Ghost.WeaponAngle == Approx(Base.WeaponAngle));
    CHECK(Ghost.Mask.all());
}

TEST_CASE("The ghost takes the wrist from the clip when it sets one", "[editor][ghost]") {
    const anim::Clip Stance = loadPose("stance_sword");
    const anim::Clip Cut = loadPose("sword_cut");
    const anim::Pose Ghost = composeGhostPose(Stance, Cut, 0.2f, BodyPart::ForearmL);
    CHECK(Ghost.WeaponAngle == Approx(anim::sampleClip(Cut, 0.2f).WeaponAngle));
    // Played in the left hand: the swapped arm is posed, the right arm is the stance's.
    CHECK(Ghost.getAngle(BodyPart::ForearmL) == Approx(anim::sampleClip(Cut, 0.2f).getAngle(BodyPart::ForearmR)));
}

TEST_CASE("The ghost time wraps for a loop and holds for a one-shot clip", "[editor][ghost]") {
    const anim::Clip Stance = loadPose("stance");
    const anim::Clip Walk = loadPose("walk");
    const anim::Pose Wrapped = composeGhostPose(Stance, Walk, Walk.DurationSec + 0.1f, std::nullopt);
    const anim::Pose Early = composeGhostPose(Stance, Walk, 0.1f, std::nullopt);
    CHECK(Wrapped.getAngle(BodyPart::ThighL) == Approx(Early.getAngle(BodyPart::ThighL)).margin(1e-4));

    const anim::Clip Jab = loadPose("jab");
    const anim::Pose End = composeGhostPose(Stance, Jab, 5.0f, std::nullopt);
    CHECK(End.getAngle(BodyPart::ForearmL) == Approx(Jab.Keys.back().Target.getAngle(BodyPart::ForearmL)));
}
