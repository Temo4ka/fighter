#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "editor/clip_check.hpp"
#include "editor/editor_config.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"
#include "editor/pose_fk.hpp"
#include "editor/trajectory.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

anim::Clip loadPose(const std::string& Name) { return anim::loadClip(DataDir / "poses" / (Name + ".json")); }

bool hasKind(const std::vector<ClipWarning>& Warnings, WarningKind Kind) {
    return std::ranges::any_of(Warnings, [Kind](const ClipWarning& Warning) { return Warning.Kind == Kind; });
}

TipPath makePath(float LowestY) {
    return {.Part = BodyPart::ForearmL,
            .Points = {{.TimeSec = 0.0f, .Tip = {0.0f, 1.0f}, .Active = false},
                       {.TimeSec = 0.1f, .Tip = {0.5f, LowestY}, .Active = true},
                       {.TimeSec = 0.2f, .Tip = {0.2f, 0.8f}, .Active = false}},
            .TravelM = 0.5f};
}

} // namespace

TEST_CASE("findTipFloorWarnings: fires below the margin only, at the lowest point", "[editor][check]") {
    const EditorConfig Config;   // the margin is 2 cm
    CHECK(findTipFloorWarnings(makePath(0.3f), WarningSource::Ghost, Config).empty());
    CHECK(findTipFloorWarnings(makePath(-0.01f), WarningSource::Ghost, Config).empty());   // within the margin
    const std::vector<ClipWarning> Warnings = findTipFloorWarnings(makePath(-0.05f), WarningSource::Fact, Config);
    REQUIRE(Warnings.size() == 1);
    CHECK(Warnings[0].Kind == WarningKind::TipBelowFloor);
    CHECK(Warnings[0].Source == WarningSource::Fact);
    CHECK(Warnings[0].TimeSec == 0.1f);
    CHECK(Warnings[0].Part == BodyPart::ForearmL);
    CHECK_THAT(Warnings[0].Text, ContainsSubstring("fact") && ContainsSubstring("5.0 cm below the floor"));
    // A larger margin lets it pass.
    EditorConfig Loose;
    Loose.TipFloorMarginM = 0.1f;
    CHECK(findTipFloorWarnings(makePath(-0.05f), WarningSource::Fact, Loose).empty());
    CHECK(findTipFloorWarnings(TipPath{}, WarningSource::Ghost, Config).empty());
}

TEST_CASE("The tip below the floor is found on the ghost path of a crafted clip", "[editor][check]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "greatsword");
    const EditorConfig Config;
    // A weapon arm hanging straight down (the reference pose) holds a long
    // blade whose point goes below the floor, which is the lowest foot.
    anim::Clip Hanging;
    Hanging.DurationSec = 0.4f;
    Hanging.ActiveBeginSec = 0.1f;
    Hanging.ActiveEndSec = 0.2f;
    Hanging.Strikers.set(static_cast<size_t>(*Context.WeaponHand));
    for (const float TimeSec : {0.0f, 0.4f}) {
        anim::Keyframe Key;
        Key.TimeSec = TimeSec;
        const BodyPart Arm = *Context.WeaponHand == BodyPart::ForearmL ? BodyPart::UpperArmL : BodyPart::UpperArmR;
        Key.Target.setAngle(Arm, 0.0f);
        Key.Target.setAngle(*Context.WeaponHand, 0.0f);
        Key.Target.setWeaponAngle(0.0f);
        Hanging.Keys.push_back(Key);
    }
    const std::optional<TipPath> Path = sampleGhostPath(Context, Hanging, true, 0.05f);
    REQUIRE(Path.has_value());
    CHECK(Path->Part == *Context.WeaponHand);
    const std::vector<ClipWarning> Warnings = findTipFloorWarnings(*Path, WarningSource::Ghost, Config);
    REQUIRE(Warnings.size() == 1);
    CHECK(Warnings[0].Source == WarningSource::Ghost);

    // Without the weapon the fist stays well above the floor.
    const std::optional<TipPath> Fist = sampleGhostPath(Context, Hanging, false, 0.05f);
    REQUIRE(Fist.has_value());
    CHECK(findTipFloorWarnings(*Fist, WarningSource::Ghost, Config).empty());
}

TEST_CASE("findJointLimitWarnings: a key at or past a limit, not one well inside", "[editor][check]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    const EditorConfig Config;
    const AngleRange Elbow = getJointRange(Context.Rig, BodyPart::ForearmL);

    anim::Clip Clip;
    Clip.DurationSec = 0.4f;
    anim::Keyframe First;
    First.TimeSec = 0.0f;
    First.Target.setAngle(BodyPart::ForearmL, (Elbow.Lower + Elbow.Upper) * 0.5f);
    Clip.Keys.push_back(First);
    anim::Keyframe Second = First;
    Second.TimeSec = 0.2f;
    Clip.Keys.push_back(Second);
    CHECK(findJointLimitWarnings(Context.Rig, Clip, Config).empty());

    Clip.Keys[1].Target.setAngle(BodyPart::ForearmL, Elbow.Upper + 0.3f);   // past
    std::vector<ClipWarning> Warnings = findJointLimitWarnings(Context.Rig, Clip, Config);
    REQUIRE(Warnings.size() == 1);
    CHECK(Warnings[0].Kind == WarningKind::JointAtLimit);
    CHECK(Warnings[0].TimeSec == 0.2f);
    CHECK(Warnings[0].Part == BodyPart::ForearmL);
    CHECK_THAT(Warnings[0].Text, ContainsSubstring("ForearmL"));

    Clip.Keys[1].Target.setAngle(BodyPart::ForearmL, Elbow.Lower);   // exactly at it
    CHECK(findJointLimitWarnings(Context.Rig, Clip, Config).size() == 1);
    // Just inside, outside the margin (1 degree).
    Clip.Keys[1].Target.setAngle(BodyPart::ForearmL, Elbow.Lower + 0.1f);
    CHECK(findJointLimitWarnings(Context.Rig, Clip, Config).empty());
    // A wider margin catches it.
    EditorConfig Wide;
    Wide.LimitMarginDeg = 10.0f;
    CHECK(findJointLimitWarnings(Context.Rig, Clip, Wide).size() == 1);
    // The lean of the root has no limit and a joint the clip does not key is not looked at.
    Clip.Keys[1].Target.setAngle(BodyPart::ForearmL, Elbow.Lower + 0.5f);
    Clip.Keys[1].Target.setAngle(BodyPart::Pelvis, 5.0f);
    CHECK(findJointLimitWarnings(Context.Rig, Clip, Config).empty());
}

TEST_CASE("findJointLimitWarnings: the wrist of the weapon", "[editor][check]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "short_sword");
    const AngleRange Wrist = getWristRange(Context.Rig);
    anim::Clip Clip;
    Clip.DurationSec = 0.4f;
    anim::Keyframe Key;
    Key.Target.setWeaponAngle((Wrist.Lower + Wrist.Upper) * 0.5f);
    Clip.Keys.push_back(Key);
    CHECK(findJointLimitWarnings(Context.Rig, Clip, EditorConfig{}).empty());
    Clip.Keys[0].Target.setWeaponAngle(Wrist.Upper + 0.2f);
    const std::vector<ClipWarning> Warnings = findJointLimitWarnings(Context.Rig, Clip, EditorConfig{});
    REQUIRE(Warnings.size() == 1);
    CHECK_THAT(Warnings[0].Text, ContainsSubstring("wrist"));
}

TEST_CASE("findStrikeWarnings: empty active, no strikers, a weapon move without an arm", "[editor][check]") {
    const anim::Clip Jab = loadPose("jab");
    CHECK(findStrikeWarnings(Jab, false).empty());
    CHECK(findStrikeWarnings(Jab, true).empty());   // the jab strikes with a forearm

    anim::Clip Empty = Jab;
    Empty.ActiveEndSec = Empty.ActiveBeginSec;
    CHECK(hasKind(findStrikeWarnings(Empty, false), WarningKind::EmptyActive));

    anim::Clip Late = Jab;
    Late.ActiveEndSec = Late.DurationSec + 0.1f;
    const std::vector<ClipWarning> Past = findStrikeWarnings(Late, false);
    REQUIRE(Past.size() == 1);
    CHECK(Past[0].Kind == WarningKind::EmptyActive);
    CHECK_THAT(Past[0].Text, ContainsSubstring("past the end"));

    anim::Clip None = Jab;
    None.Strikers.reset();
    const std::vector<ClipWarning> NoStrikers = findStrikeWarnings(None, true);
    CHECK(hasKind(NoStrikers, WarningKind::NoStrikers));
    CHECK_FALSE(hasKind(NoStrikers, WarningKind::WeaponNotHeld));

    const anim::Clip Kick = loadPose("kick");
    CHECK(findStrikeWarnings(Kick, false).empty());
    const std::vector<ClipWarning> Mismatch = findStrikeWarnings(Kick, true);
    REQUIRE(Mismatch.size() == 1);
    CHECK(Mismatch[0].Kind == WarningKind::WeaponNotHeld);
}

TEST_CASE("findDivergenceWarnings: fires when the threshold is below the fact's error, not above", "[editor][check]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    const anim::Clip Jab = loadPose("jab");
    PhysicsPreview Preview({.DataDir = DataDir, .ClipName = "jab", .WeaponId = {}, .MoveId = {}, .WithDummy = false});
    Preview.setClip(Jab);

    EditorConfig Strict;
    Strict.DivergenceDeg = 1.0f;
    const std::vector<ClipWarning> Fired = findDivergenceWarnings(Context, Jab, Preview, Strict);
    REQUIRE(Fired.size() == 1);
    CHECK(Fired[0].Kind == WarningKind::Divergence);
    CHECK(Fired[0].Source == WarningSource::Fact);
    CHECK(Fired[0].Part.has_value());
    // The time is inside the active phase of the clip (the frame's clip time).
    CHECK(Fired[0].TimeSec >= Jab.ActiveBeginSec - 0.05f);
    CHECK(Fired[0].TimeSec <= Jab.ActiveEndSec + 0.05f);

    EditorConfig Loose;
    Loose.DivergenceDeg = 180.0f;
    CHECK(findDivergenceWarnings(Context, Jab, Preview, Loose).empty());
}

TEST_CASE("findDivergenceWarnings: a move that cannot run has no warning", "[editor][check]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    PhysicsPreview Preview({.DataDir = DataDir, .ClipName = "stance", .WeaponId = {}, .MoveId = {}, .WithDummy = false});
    CHECK(findDivergenceWarnings(Context, loadPose("stance"), Preview, EditorConfig{}).empty());
}

TEST_CASE("getSourceLabel names the sources", "[editor][check]") {
    CHECK(std::string(getSourceLabel(WarningSource::Clip)) == "clip");
    CHECK(std::string(getSourceLabel(WarningSource::Ghost)) == "ghost");
    CHECK(std::string(getSourceLabel(WarningSource::Fact)) == "fact");
}
