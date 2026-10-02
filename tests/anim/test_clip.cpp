#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <numbers>
#include <stdexcept>
#include <string>

#include "anim/clip.hpp"
#include "anim/pose.hpp"

using namespace fighter;
using namespace fighter::anim;
using Catch::Approx;

namespace {

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

constexpr const char* OneShotJson = R"({
    "duration": 1.0,
    "active": [0.2, 0.4],
    "strikers": ["ForearmL"],
    "stiffness": 1.5,
    "allowMove": false,
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

} // namespace

TEST_CASE("parseClip: fields and keys", "[anim]") {
    const Clip Jab = parseClip(OneShotJson, "jab");
    CHECK(Jab.Name == "jab");
    CHECK_FALSE(Jab.Loop);
    CHECK(Jab.Keys.size() == 2);
    CHECK(Jab.Stiffness == 1.5f);
    CHECK_FALSE(Jab.AllowMove);
    CHECK(Jab.isStriker(BodyPart::ForearmL));
    CHECK_FALSE(Jab.isStriker(BodyPart::ForearmR));
    CHECK_FALSE(Jab.isActiveAt(0.1f));
    CHECK(Jab.isActiveAt(0.3f));
    CHECK_FALSE(Jab.isActiveAt(0.4f));
    CHECK(Jab.isFinishedAt(1.0f));
    // Angles are given in degrees and stored in radians.
    CHECK(Jab.Keys[1].Target.getAngle(BodyPart::ForearmL) == Approx(90.0f * RadiansPerDegree));
    CHECK(Jab.Keys[1].Target.hasJoint(BodyPart::ForearmL));
    CHECK_FALSE(Jab.Keys[1].Target.hasJoint(BodyPart::ForearmR));
}

TEST_CASE("sampleClip: interpolates, holds and loops", "[anim]") {
    const Clip OneShot = parseClip(OneShotJson, "jab");
    CHECK(sampleClip(OneShot, 0.25f).getAngle(BodyPart::ForearmL) == Approx(45.0f * RadiansPerDegree));
    // A one-shot clip holds its last key.
    CHECK(sampleClip(OneShot, 0.9f).getAngle(BodyPart::ForearmL) == Approx(90.0f * RadiansPerDegree));
    CHECK(sampleClip(OneShot, 5.0f).getAngle(BodyPart::ForearmL) == Approx(90.0f * RadiansPerDegree));

    const Clip Loop = parseClip(LoopJson, "walk");
    // After the last key a looping clip blends back into the first one.
    CHECK(sampleClip(Loop, 0.75f).getAngle(BodyPart::ThighL) == Approx(20.0f * RadiansPerDegree));
    CHECK(sampleClip(Loop, 1.25f).getAngle(BodyPart::ThighL) == Approx(20.0f * RadiansPerDegree));
    CHECK(sampleClip(Loop, -0.25f).getAngle(BodyPart::ThighL) == Approx(20.0f * RadiansPerDegree));
}

TEST_CASE("layerPose: the top pose overrides only its joints", "[anim]") {
    Pose Base;
    Base.setAngle(BodyPart::ThighL, 1.0f);
    Base.setAngle(BodyPart::ForearmL, 2.0f);
    Pose Top;
    Top.setAngle(BodyPart::ForearmL, 3.0f);
    Top.setAngle(BodyPart::Head, 0.5f);

    layerPose(Base, Top);
    CHECK(Base.getAngle(BodyPart::ThighL) == 1.0f);
    CHECK(Base.getAngle(BodyPart::ForearmL) == 3.0f);
    CHECK(Base.getAngle(BodyPart::Head) == 0.5f);
    CHECK(Base.hasJoint(BodyPart::Head));
}

TEST_CASE("parseClip: broken clips are rejected", "[anim]") {
    CHECK_THROWS_AS(parseClip("{", "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "keys": [] })", "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "keys": [{ "t": 0.1, "pose": {} }] })", "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "keys": [{ "t": 0, "pose": { "Tail": 1 } }] })", "x"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "stifness": 2, "keys": [{ "t": 0, "pose": {} }] })", "x"),
                    std::runtime_error);
    // Every key must set the same joints.
    CHECK_THROWS_AS(parseClip(R"({ "duration": 1, "keys": [{ "t": 0, "pose": { "Head": 1 } },
                                                           { "t": 0.5, "pose": { "Torso": 1 } }] })",
                              "x"),
                    std::runtime_error);
}

TEST_CASE("loadClip: the clips of data/poses load", "[anim]") {
    const std::filesystem::path Poses = std::filesystem::path(FIGHTER_DATA_DIR) / "poses";
    for (const auto* Name : {"stance", "walk", "jab", "kick"}) {
        const Clip Loaded = loadClip(Poses / (std::string(Name) + ".json"));
        CHECK(Loaded.Name == Name);
        CHECK_FALSE(Loaded.Keys.empty());
    }
    // The stance is the base layer, so it sets every joint.
    const Clip Stance = loadClip(Poses / "stance.json");
    CHECK(Stance.Keys.front().Target.Mask.count() == BodyPartCount);
}
