#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"
#include "editor/pose_fk.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

anim::Clip loadPose(const std::string& Name) { return anim::loadClip(DataDir / "poses" / (Name + ".json")); }

PhysicsPreview makePreview(const std::string& Clip, bool WithDummy = false) {
    return PhysicsPreview({.DataDir = DataDir, .ClipName = Clip, .WeaponId = {}, .WithDummy = WithDummy});
}

/// The two frames are the same state.
void checkSameFrame(const FactFrame& First, const FactFrame& Second) {
    CHECK(First.ClipSec == Second.ClipSec);
    CHECK(First.Phase == Second.Phase);
    REQUIRE(First.Attacker.Parts.size() == Second.Attacker.Parts.size());
    for (const auto& [Left, Right] : std::views::zip(First.Attacker.Parts, Second.Attacker.Parts)) {
        CHECK(Left.Position.X == Right.Position.X);
        CHECK(Left.Position.Y == Right.Position.Y);
        CHECK(Left.Angle == Right.Angle);
    }
    CHECK(First.Dummy.Position.X == Second.Dummy.Position.X);
}

/// Every frame of the move, computed to the end.
std::vector<FactFrame> runToEnd(PhysicsPreview& Preview) {
    std::vector<FactFrame> Frames;
    for (size_t Index = 0; const FactFrame* Frame = Preview.getFrame(Index); ++Index) Frames.push_back(*Frame);
    return Frames;
}

} // namespace

TEST_CASE("PhysicsPreview: the move of the clip runs and its frames follow the clip time", "[editor][preview]") {
    PhysicsPreview Preview = makePreview("jab");
    REQUIRE(Preview.getProblem().empty());
    REQUIRE(Preview.getSetup() != nullptr);
    CHECK(Preview.getSetup()->MoveId == "jab");
    CHECK(Preview.getSetup()->ClipName == "jab");
    const std::vector<FactFrame> Frames = runToEnd(Preview);
    REQUIRE(Frames.size() > 10);
    CHECK(Preview.isComplete());
    CHECK(Preview.getProblem().empty());
    // The clip time grows from the start of the move; the fighter's pelvis
    // starts at x = 0, where the ghost's is.
    CHECK(Frames.front().ClipSec < 0.05f);
    for (const auto& [Before, After] : std::views::zip(Frames, Frames | std::views::drop(1))) {
        CHECK(After.ClipSec >= Before.ClipSec);
    }
    CHECK(Frames.front().Attacker.Parts[static_cast<size_t>(BodyPart::Pelvis)].Position.X == Approx(0.0f).margin(0.02f));
    // Clip time t shows the first frame that reached it.
    const float Middle = Frames[Frames.size() / 2].ClipSec;
    const std::optional<size_t> Found = Preview.findFrame(Middle);
    REQUIRE(Found);
    CHECK(Frames[*Found].ClipSec >= Middle);
    if (*Found > 0) CHECK(Frames[*Found - 1].ClipSec < Middle);
    // Past the end: the last frame.
    CHECK(Preview.findFrame(100.0f) == Frames.size() - 1);
}

TEST_CASE("PhysicsPreview: an unsaved clip equal to its file gives the frames of the file", "[editor][preview]") {
    PhysicsPreview FromFile = makePreview("jab");
    PhysicsPreview InMemory = makePreview("jab");
    CHECK(InMemory.setClip(loadPose("jab")));
    const std::vector<FactFrame> Expected = runToEnd(FromFile);
    const std::vector<FactFrame> Actual = runToEnd(InMemory);
    REQUIRE(Actual.size() == Expected.size());
    for (const auto& [Left, Right] : std::views::zip(Expected, Actual)) checkSameFrame(Left, Right);
}

TEST_CASE("PhysicsPreview: two runs give the same states", "[editor][preview]") {
    PhysicsPreview First = makePreview("jab", true);
    PhysicsPreview Second = makePreview("jab", true);
    const std::vector<FactFrame> One = runToEnd(First);
    const std::vector<FactFrame> Two = runToEnd(Second);
    REQUIRE(One.size() == Two.size());
    for (const auto& [Left, Right] : std::views::zip(One, Two)) checkSameFrame(Left, Right);
}

TEST_CASE("PhysicsPreview: scrubbing back to a time gives the state of running forward to it", "[editor][preview]") {
    PhysicsPreview Scrubbed = makePreview("jab");
    runToEnd(Scrubbed);
    const size_t RunsBefore = Scrubbed.getRunCount();
    const std::optional<size_t> Back = Scrubbed.findFrame(0.1f);
    REQUIRE(Back);
    // Read from the cache, not run again.
    CHECK(Scrubbed.getRunCount() == RunsBefore);

    PhysicsPreview Forward = makePreview("jab");
    const std::optional<size_t> Ahead = Forward.findFrame(0.1f);
    REQUIRE(Ahead == Back);
    // Only as far as needed.
    CHECK_FALSE(Forward.isComplete());
    checkSameFrame(*Scrubbed.getFrame(*Back), *Forward.getFrame(*Ahead));
}

TEST_CASE("PhysicsPreview: an edit drops the cache and the next run plays it", "[editor][preview]") {
    PhysicsPreview Preview = makePreview("jab");
    anim::Clip Edited = loadPose("jab");
    Preview.setClip(Edited);
    const std::vector<FactFrame> Before = runToEnd(Preview);
    const size_t Runs = Preview.getRunCount();
    // The same clip again changes nothing.
    CHECK_FALSE(Preview.setClip(Edited));
    CHECK(Preview.getFrameCount() == Before.size());

    // A slower clip: everything twice as long.
    for (anim::Keyframe& Key : Edited.Keys) Key.TimeSec *= 2.0f;
    Edited.DurationSec *= 2.0f;
    Edited.ActiveBeginSec *= 2.0f;
    Edited.ActiveEndSec *= 2.0f;
    CHECK(Preview.setClip(Edited));
    CHECK(Preview.getFrameCount() == 0);
    CHECK_FALSE(Preview.isComplete());
    const std::vector<FactFrame> After = runToEnd(Preview);
    CHECK(Preview.getRunCount() == Runs + 1);
    CHECK(After.size() > Before.size());
}

TEST_CASE("PhysicsPreview: the dummy switch drops the cache", "[editor][preview]") {
    PhysicsPreview Preview = makePreview("jab");
    runToEnd(Preview);
    CHECK(Preview.setWithDummy(true));
    CHECK(Preview.isWithDummy());
    CHECK(Preview.getFrameCount() == 0);
    CHECK_FALSE(Preview.setWithDummy(true));
    const std::vector<FactFrame> Frames = runToEnd(Preview);
    REQUIRE_FALSE(Frames.empty());
    // The dummy stands near, at the move's range.
    CHECK(Frames.front().Dummy.Position.X < 3.0f);
}

TEST_CASE("PhysicsPreview: a close clip plays with the opponent near", "[editor][preview]") {
    PhysicsPreview Preview = makePreview("jab_close", true);
    REQUIRE(Preview.getProblem().empty());
    REQUIRE(Preview.getSetup() != nullptr);
    CHECK(Preview.getSetup()->MoveId == "jab");
    CHECK(runToEnd(Preview).size() > 10);
    CHECK(Preview.getSetup()->ClipName == "jab_close");
}

TEST_CASE("PhysicsPreview: a clip no move plays, and a broken clip, are problems", "[editor][preview]") {
    PhysicsPreview Stance = makePreview("stance");
    CHECK_THAT(Stance.getProblem(), ContainsSubstring("no move"));
    CHECK_FALSE(Stance.findFrame(0.0f));

    PhysicsPreview Broken = makePreview("jab");
    anim::Clip Edited = loadPose("jab");
    Edited.Keys.clear();
    Broken.setClip(Edited);
    CHECK_FALSE(Broken.findFrame(0.0f));
    CHECK_THAT(Broken.getProblem(), ContainsSubstring("jab"));
    // Fixing it runs again.
    Broken.setClip(loadPose("jab"));
    CHECK(Broken.findFrame(0.0f));
    CHECK(Broken.getProblem().empty());
}

TEST_CASE("compareToTarget: joint errors of the upper body, fact minus target", "[editor][preview]") {
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    const PosedBody Target = poseBody(Context.Rig, anim::sampleClip(Context.Stance, 0.0f), Context.Held);
    combat::FighterView Fact;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        Fact.Parts.push_back({.Part = Part, .Position = Target.get(Part).Position, .Angle = Target.get(Part).Angle,
                              .Size = {}});
    }
    // The same pose: no error.
    for (const JointError& Error : compareToTarget(Context.Rig, Target, Fact)) {
        CHECK(Error.ErrorRad == Approx(0.0f).margin(1e-5f));
    }
    // The left forearm turned on: only the elbow is off, the legs are not compared.
    Fact.Parts[static_cast<size_t>(BodyPart::ForearmL)].Angle += 0.3f;
    Fact.Parts[static_cast<size_t>(BodyPart::ShinL)].Angle += 1.0f;
    const std::vector<JointError> Errors = compareToTarget(Context.Rig, Target, Fact);
    const JointError* Worst = findWorstJoint(Errors);
    REQUIRE(Worst != nullptr);
    CHECK(Worst->Part == BodyPart::ForearmL);
    CHECK(Worst->ErrorRad == Approx(0.3f));
    for (const JointError& Error : Errors) {
        CHECK(Error.Part != BodyPart::ShinL);
        CHECK(Error.Part != BodyPart::Pelvis);
    }
    // A turn by a whole circle is no error.
    Fact.Parts[static_cast<size_t>(BodyPart::ForearmL)].Angle += 2.0f * std::numbers::pi_v<float> - 0.3f;
    const std::vector<JointError> Turned = compareToTarget(Context.Rig, Target, Fact);
    CHECK(findWorstJoint(Turned)->ErrorRad == Approx(0.0f).margin(1e-4f));
    CHECK(findWorstJoint({}) == nullptr);
}

TEST_CASE("compareToTarget: the fact at the start of the move is near the ghost", "[editor][preview]") {
    // The stand fighter at the first frame of the jab: the stance is posed by
    // motors, so its joints are near the ghost's (not a feel check, only that
    // the frames are compared the right way round).
    const GhostContext Context = loadGhostContext(DataDir.parent_path(), "");
    PhysicsPreview Preview = makePreview("jab");
    const FactFrame* Frame = Preview.getFrame(0);
    REQUIRE(Frame != nullptr);
    const PosedBody Target = poseBody(Context.Rig, anim::sampleClip(Context.Stance, 0.0f), Context.Held);
    const std::vector<JointError> Errors = compareToTarget(Context.Rig, Target, Frame->Attacker);
    const JointError* Worst = findWorstJoint(Errors);
    REQUIRE(Worst != nullptr);
    CHECK(std::abs(Worst->ErrorRad) < 0.5f);
}
