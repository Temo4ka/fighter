#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "anim/playback.hpp"
#include "anim/pose.hpp"

using namespace fighter;
using namespace fighter::anim;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path PosesDir = std::filesystem::path(FIGHTER_DATA_DIR) / "poses";

constexpr float Dt = 1.0f / 60.0f;

constexpr const char* AttackJson = R"({
    "duration": 1.0,
    "active": [0.2, 0.4],
    "strikers": ["ForearmL"],
    "keys": [
        { "t": 0.0, "pose": { "ForearmL": 0 } },
        { "t": 0.5, "pose": { "ForearmL": 90 } }
    ]
})";

constexpr const char* LoopJson = R"({
    "loop": true,
    "duration": 1.0,
    "keys": [
        { "t": 0.0, "pose": { "ThighL": 0 } },
        { "t": 0.5, "pose": { "ThighL": 40 } }
    ]
})";

Pose makePose(float Head, float Torso) {
    Pose Result;
    Result.setAngle(BodyPart::Head, Head);
    Result.setAngle(BodyPart::Torso, Torso);
    return Result;
}

/// The largest difference of any joint set in both poses, rad.
float getLargestDifference(const Pose& A, const Pose& B) {
    float Largest = 0.0f;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (A.Mask.test(Index) && B.Mask.test(Index)) {
            Largest = std::max(Largest, std::abs(A.Angles[Index] - B.Angles[Index]));
        }
    }
    return Largest;
}

} // namespace

TEST_CASE("blendPoses: endpoints, middle and clamping", "[anim][playback]") {
    const Pose From = makePose(0.0f, 1.0f);
    const Pose To = makePose(2.0f, -1.0f);
    CHECK(blendPoses(From, To, 0.0f).getAngle(BodyPart::Head) == 0.0f);
    CHECK(blendPoses(From, To, 1.0f).getAngle(BodyPart::Head) == Approx(2.0f));
    CHECK(blendPoses(From, To, 0.5f).getAngle(BodyPart::Head) == Approx(1.0f));
    CHECK(blendPoses(From, To, 0.5f).getAngle(BodyPart::Torso) == Approx(0.0f));
    CHECK(blendPoses(From, To, -3.0f).getAngle(BodyPart::Head) == 0.0f);
    CHECK(blendPoses(From, To, 7.0f).getAngle(BodyPart::Head) == Approx(2.0f));
}

TEST_CASE("blendPoses: a joint only one pose sets is taken as it is", "[anim][playback]") {
    Pose From;
    From.setAngle(BodyPart::Head, 1.0f);
    From.setAngle(BodyPart::ThighL, 0.7f);
    Pose To;
    To.setAngle(BodyPart::Head, 3.0f);
    To.setAngle(BodyPart::Torso, 0.4f);

    const Pose Half = blendPoses(From, To, 0.5f);
    CHECK(Half.getAngle(BodyPart::Head) == Approx(2.0f));
    CHECK(Half.getAngle(BodyPart::ThighL) == 0.7f);   // only in From
    CHECK(Half.getAngle(BodyPart::Torso) == 0.4f);    // only in To
    CHECK(Half.hasJoint(BodyPart::ThighL));
    CHECK(Half.hasJoint(BodyPart::Torso));
    CHECK_FALSE(Half.hasJoint(BodyPart::ForearmR));
}

TEST_CASE("PoseTransition: fades from the start pose to the target", "[anim][playback]") {
    PoseTransition Fade;
    const Pose Start = makePose(0.0f, 0.0f);
    const Pose Target = makePose(1.0f, -1.0f);

    // No fade: the target as it is.
    CHECK_FALSE(Fade.isActive());
    CHECK(Fade.getWeight() == 1.0f);
    CHECK(Fade.step(Target, Dt).getAngle(BodyPart::Head) == 1.0f);

    Fade.begin(Start, 0.2f);
    CHECK(Fade.isActive());
    CHECK(Fade.getWeight() == 0.0f);

    float Previous = 0.0f;
    int Ticks = 0;
    while (Fade.isActive()) {
        const Pose Shown = Fade.step(Target, Dt);
        ++Ticks;
        const float Head = Shown.getAngle(BodyPart::Head);
        CHECK(Head >= Previous);
        CHECK(Head <= 1.0f + 1e-5f);
        Previous = Head;
        REQUIRE(Ticks < 100);
    }
    // 0.2 s is 12 ticks of 1/60 s; the last one lands on the target.
    CHECK(Ticks >= 11);
    CHECK(Ticks <= 13);
    CHECK(Fade.getWeight() == 1.0f);
    CHECK(Fade.step(Target, Dt).getAngle(BodyPart::Torso) == -1.0f);
}

TEST_CASE("PoseTransition: peek blends another target at the same weight", "[anim][playback]") {
    PoseTransition Fade;
    const Pose Start = makePose(0.0f, 0.0f);
    CHECK(Fade.peek(makePose(1.0f, 1.0f)).getAngle(BodyPart::Head) == 1.0f);   // no fade
    Fade.begin(Start, 0.2f);
    const Pose Shown = Fade.step(makePose(1.0f, 1.0f), 0.1f);
    const Pose Other = Fade.peek(makePose(2.0f, 2.0f));
    CHECK(Other.getAngle(BodyPart::Head) == Approx(2.0f * Shown.getAngle(BodyPart::Head)));
    // Peeking does not advance the fade.
    CHECK(Fade.getWeight() == Approx(0.5f));
    CHECK(Fade.peek(makePose(2.0f, 2.0f)).getAngle(BodyPart::Head) == Approx(Other.getAngle(BodyPart::Head)));
}

TEST_CASE("PoseTransition: the first shown pose is close to the old one", "[anim][playback]") {
    PoseTransition Fade;
    Fade.begin(makePose(0.0f, 0.0f), 0.1f);
    const Pose First = Fade.step(makePose(1.0f, 1.0f), Dt);
    // Eased: the first tick of a 6-tick fade moves a few percent, not 1/6.
    CHECK(First.getAngle(BodyPart::Head) > 0.0f);
    CHECK(First.getAngle(BodyPart::Head) < 0.1f);
}

TEST_CASE("PoseTransition: a zero or negative time is no fade", "[anim][playback]") {
    PoseTransition Fade;
    Fade.begin(makePose(0.0f, 0.0f), 0.0f);
    CHECK_FALSE(Fade.isActive());
    CHECK(Fade.step(makePose(1.0f, 1.0f), Dt).getAngle(BodyPart::Head) == 1.0f);
    Fade.begin(makePose(0.0f, 0.0f), -1.0f);
    CHECK_FALSE(Fade.isActive());
}

TEST_CASE("PoseTransition: begin during a fade continues from the shown pose", "[anim][playback]") {
    PoseTransition Fade;
    Fade.begin(makePose(0.0f, 0.0f), 0.3f);
    Pose Shown;
    for (int Tick = 0; Tick < 6; ++Tick) Shown = Fade.step(makePose(1.0f, 1.0f), Dt);
    REQUIRE(Fade.isActive());

    // The target jumps again (another clip starts): the new fade starts from
    // what is on screen, so nothing jumps.
    Fade.begin(Shown, 0.3f);
    const Pose Next = Fade.step(makePose(-1.0f, -1.0f), Dt);
    CHECK(getLargestDifference(Shown, Next) < 0.05f);
}

TEST_CASE("PoseTransition: cancel drops the fade", "[anim][playback]") {
    PoseTransition Fade;
    Fade.begin(makePose(0.0f, 0.0f), 1.0f);
    Fade.cancel();
    CHECK_FALSE(Fade.isActive());
    CHECK(Fade.step(makePose(1.0f, 1.0f), Dt).getAngle(BodyPart::Head) == 1.0f);
}

TEST_CASE("PoseTransition: the same calls give the same poses", "[anim][playback]") {
    const auto Run = [] {
        PoseTransition Fade;
        std::vector<float> Heads;
        Fade.begin(makePose(0.3f, 0.1f), 0.25f);
        for (int Tick = 0; Tick < 30; ++Tick) {
            if (Tick == 8) Fade.begin(makePose(0.9f, 0.2f), 0.1f);
            Heads.push_back(Fade.step(makePose(-0.5f, 0.7f), Dt).getAngle(BodyPart::Head));
        }
        return Heads;
    };
    CHECK(Run() == Run());
}

TEST_CASE("PoseTransition: switching clips has no jumps with a fade", "[anim][playback]") {
    // Walking, then a kick starts (its legs are far from the walk pose), then
    // a block replaces it. Without the fade the pose jumps at each switch.
    const Clip Stance = loadClip(PosesDir / "stance.json");
    const Clip Walk = loadClip(PosesDir / "walk.json");
    const Clip Kick = loadClip(PosesDir / "kick.json");
    const Clip Block = loadClip(PosesDir / "block_high.json");

    const auto Layered = [&](float WalkTime, const Clip* Over, float OverTime) {
        Pose Target = sampleClip(Stance, 0.0f);
        layerPose(Target, sampleClip(Walk, WalkTime));
        if (Over != nullptr) layerPose(Target, sampleClip(*Over, OverTime));
        return Target;
    };

    // A clip without its own fade time gets the blend table's (combat); this
    // one is the table's default.
    constexpr float SwitchFadeSec = 0.1f;
    // The step the shown pose makes on the first tick after each of the two
    // switches, the largest of them, rad.
    const auto Play = [&](bool UseFade) {
        PoseTransition Fade;
        float Largest = 0.0f;
        Pose Shown = Layered(0.25f, nullptr, 0.0f);
        const auto Show = [&](const Pose& Target) {
            const Pose Next = UseFade ? Fade.step(Target, Dt) : Target;
            const float Step = getLargestDifference(Shown, Next);
            Shown = Next;
            return Step;
        };
        // 0.2 s of walking.
        for (int Tick = 0; Tick < 12; ++Tick) Show(Layered(0.25f + Dt * static_cast<float>(Tick), nullptr, 0.0f));
        // The kick starts.
        if (UseFade) Fade.begin(Shown, Kick.BlendInSec.value_or(SwitchFadeSec));
        for (int Tick = 0; Tick < 12; ++Tick) {
            const float Step = Show(Layered(0.45f, &Kick, 0.001f + Dt * static_cast<float>(Tick)));
            if (Tick == 0) Largest = std::max(Largest, Step);
        }
        // The block starts while the kick is still playing (a reaction cuts in).
        if (UseFade) Fade.begin(Shown, Block.BlendInSec.value_or(SwitchFadeSec));
        for (int Tick = 0; Tick < 12; ++Tick) {
            Pose Target = Layered(0.45f, &Kick, 0.2f);
            layerPose(Target, sampleClip(Block, 0.0f));
            const float Step = Show(Target);
            if (Tick == 0) Largest = std::max(Largest, Step);
        }
        return Largest;
    };

    const float WithoutFade = Play(false);
    const float WithFade = Play(true);
    CHECK(WithoutFade > 0.3f);
    CHECK(WithFade < 0.1f);
    CHECK(WithFade < WithoutFade * 0.25f);
}

TEST_CASE("advanceClipTime: rate, loop and one-shot", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    CHECK(advanceClipTime(Attack, 0.1f, 0.1f, 1.0f) == Approx(0.2f));
    CHECK(advanceClipTime(Attack, 0.1f, 0.1f, 2.0f) == Approx(0.3f));
    CHECK(advanceClipTime(Attack, 0.1f, 0.1f, 0.5f) == Approx(0.15f));
    // A one-shot clip stops at its end and is finished there.
    CHECK(advanceClipTime(Attack, 0.95f, 0.1f, 1.0f) == 1.0f);
    CHECK(Attack.isFinishedAt(advanceClipTime(Attack, 0.95f, 0.1f, 1.0f)));

    const Clip Loop = parseClip(LoopJson, "walk");
    CHECK(advanceClipTime(Loop, 0.9f, 0.2f, 1.0f) == Approx(0.1f));
    CHECK(advanceClipTime(Loop, 0.9f, 0.1f, 3.0f) == Approx(0.2f));
}

TEST_CASE("advanceClipTime: a rate that is too low is raised", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    CHECK(advanceClipTime(Attack, 0.0f, 1.0f, 0.0f) == Approx(MinPlaybackRate));
    CHECK(advanceClipTime(Attack, 0.0f, 1.0f, -2.0f) == Approx(MinPlaybackRate));
    CHECK(getDurationAtRate(Attack, 0.0f) == Approx(1.0f / MinPlaybackRate));
}

TEST_CASE("sampleClipAtRate: the same pose at half the real time and double the rate", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    const float Expected = sampleClip(Attack, 0.3f).getAngle(BodyPart::ForearmL);
    CHECK(sampleClipAtRate(Attack, 0.3f, 1.0f).getAngle(BodyPart::ForearmL) == Approx(Expected));
    CHECK(sampleClipAtRate(Attack, 0.15f, 2.0f).getAngle(BodyPart::ForearmL) == Approx(Expected));
    CHECK(sampleClipAtRate(Attack, 0.6f, 0.5f).getAngle(BodyPart::ForearmL) == Approx(Expected));
}

TEST_CASE("Clip rate: duration and active phase in real time", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    CHECK(getDurationAtRate(Attack, 1.0f) == Approx(1.0f));
    CHECK(getDurationAtRate(Attack, 1.25f) == Approx(0.8f));
    CHECK(getStartupAtRate(Attack, 1.0f) == Approx(0.2f));
    CHECK(getStartupAtRate(Attack, 1.25f) == Approx(0.16f));
    CHECK(getStartupAtRate(Attack, 0.8f) == Approx(0.25f));
    CHECK(getActiveEndAtRate(Attack, 2.0f) == Approx(0.2f));
}

TEST_CASE("Clip rate: the rate that gives a wanted startup", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    CHECK(getRateForStartup(Attack, 0.2f) == Approx(1.0f));
    CHECK(getRateForStartup(Attack, 0.16f) == Approx(1.25f));
    CHECK(getRateForStartup(Attack, 0.4f) == Approx(0.5f));
    // The rate really gives that startup.
    CHECK(getStartupAtRate(Attack, getRateForStartup(Attack, 0.17f)) == Approx(0.17f));
    // Nothing to compute without an active phase or with a useless wish.
    const Clip Loop = parseClip(LoopJson, "walk");
    CHECK(getRateForStartup(Loop, 0.2f) == 1.0f);
    CHECK(getRateForStartup(Attack, 0.0f) == 1.0f);
}

TEST_CASE("describePlayback: the line for the panel", "[anim][playback]") {
    const Clip Attack = parseClip(AttackJson, "attack");
    PoseTransition Fade;

    const std::string Startup = describePlayback(Attack, 0.1f, 1.25f, Fade);
    CHECK_THAT(Startup, ContainsSubstring("attack 0.10/1.00 s"));
    CHECK_THAT(Startup, ContainsSubstring("x1.25"));
    CHECK_THAT(Startup, ContainsSubstring("startup"));
    CHECK_THAT(Startup, ContainsSubstring("0.16 s"));
    CHECK_THAT(Startup, !ContainsSubstring("blend"));

    CHECK_THAT(describePlayback(Attack, 0.3f, 1.0f, Fade), ContainsSubstring("active"));
    CHECK_THAT(describePlayback(Attack, 0.7f, 1.0f, Fade), ContainsSubstring("recovery"));

    Fade.begin(makePose(0.0f, 0.0f), 0.2f);
    Fade.step(makePose(1.0f, 1.0f), 0.1f);
    CHECK_THAT(describePlayback(Attack, 0.1f, 1.0f, Fade), ContainsSubstring("blend 0.50"));

    const Clip Loop = parseClip(LoopJson, "walk");
    CHECK_THAT(describePlayback(Loop, 0.3f, 1.0f, PoseTransition{}), ContainsSubstring("loop"));
}

TEST_CASE("parseClip: fade times and the active window are read and checked", "[anim][clips]") {
    const Clip Defaults = parseClip(AttackJson, "attack");
    // Without its own times the clip leaves them to the blend table.
    CHECK_FALSE(Defaults.BlendInSec);
    CHECK_FALSE(Defaults.BlendOutSec);

    const Clip Own = parseClip(R"({ "duration": 1, "blendIn": 0.02, "blendOut": 0.3,
                                    "keys": [{ "t": 0, "pose": { "Head": 1 } }] })", "own");
    CHECK(Own.BlendInSec.value_or(-1.0f) == Approx(0.02f));
    CHECK(Own.BlendOutSec.value_or(-1.0f) == Approx(0.3f));

    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "blendIn": -0.1, "keys": [{ "t": 0, "pose": { "Head": 1 } }] })", "x"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "active": [0.5, 0.2], "keys": [{ "t": 0, "pose": { "Head": 1 } }] })",
                              "x"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "active": [0.5, 1.5], "keys": [{ "t": 0, "pose": { "Head": 1 } }] })",
                              "x"),
                    std::runtime_error);
}
