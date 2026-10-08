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

TEST_CASE("parseClip: the pelvis track", "[anim][pelvis]") {
    const Clip Lunge = parseClip(R"({
        "duration": 1.0,
        "pelvisX": [ { "t": 0.0, "x": 0.0 }, { "t": 0.4, "x": 0.2 }, { "t": 0.8, "x": 0.1 } ],
        "keys": [ { "t": 0.0, "pose": { "Torso": 0 } } ]
    })",
                                 "lunge");
    REQUIRE(Lunge.PelvisTrack.size() == 3);
    CHECK(samplePelvisOffset(Lunge, 0.0f) == Approx(0.0f));
    CHECK(samplePelvisOffset(Lunge, 0.2f) == Approx(0.1f));
    CHECK(samplePelvisOffset(Lunge, 0.6f) == Approx(0.15f));
    // After the last key the offset holds.
    CHECK(samplePelvisOffset(Lunge, 1.0f) == Approx(0.1f));
    // Without a track nothing moves.
    CHECK(samplePelvisOffset(parseClip(OneShotJson, "jab"), 0.5f) == 0.0f);
}

TEST_CASE("parseClip: broken pelvis tracks are rejected", "[anim][pelvis]") {
    const auto makeClip = [](std::string Track, bool Loop = false) {
        return std::string(R"({ "duration": 1.0, "loop": )") + (Loop ? "true" : "false") + R"(, "pelvisX": )" +
               Track + R"(, "keys": [ { "t": 0.0, "pose": { "Torso": 0 } } ] })";
    };
    CHECK_NOTHROW(parseClip(makeClip(R"([{ "t": 0, "x": 0 }, { "t": 0.5, "x": -0.3 }])"), "x"));
    // An unknown key, a start off 0, unsorted keys, a key after the end,
    // an offset out of range, a looping clip.
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0, "y": 1 }])"), "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0.1 }])"), "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0.1, "x": 0 }])"), "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0 }, { "t": 0.6, "x": 0.1 }, { "t": 0.5, "x": 0 }])"),
                              "x"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0 }, { "t": 1.5, "x": 0.1 }])"), "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0 }, { "t": 0.5, "x": 2.0 }])"), "x"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"([{ "t": 0, "x": 0 }, { "t": 0.5, "x": 0.1 }])", true), "x"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseClip(makeClip(R"({ "t": 0, "x": 0 })"), "x"), std::runtime_error);
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

TEST_CASE("parseClip: the Weapon key is the wrist, interpolated like a joint", "[anim]") {
    const Clip Parsed = parseClip(R"({
        "duration": 1.0,
        "keys": [
            { "t": 0.0, "pose": { "ForearmL": 0, "Weapon": 0 } },
            { "t": 0.5, "pose": { "ForearmL": 90, "Weapon": 80 } }
        ]
    })", "wrist");
    const Pose Half = sampleClip(Parsed, 0.25f);
    REQUIRE(Half.HasWeapon);
    CHECK(Half.WeaponAngle == Approx(40.0f * RadiansPerDegree));
    // The wrist is no body part's joint.
    CHECK(Half.Mask.count() == 1);
    CHECK_FALSE(sampleClip(parseClip(OneShotJson, "plain"), 0.25f).HasWeapon);
}

TEST_CASE("parseClip: every key sets the wrist or none does", "[anim]") {
    CHECK_THROWS_AS(parseClip(R"({
        "duration": 1.0,
        "keys": [
            { "t": 0.0, "pose": { "ForearmL": 0, "Weapon": 0 } },
            { "t": 0.5, "pose": { "ForearmL": 90 } }
        ]
    })", "uneven"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({
        "duration": 1.0,
        "keys": [ { "t": 0.0, "pose": { "Weapon": 200 } } ]
    })", "too_far"), std::runtime_error);
}

TEST_CASE("layerPose and blendPoses: the wrist of the top pose wins, blends from below", "[anim]") {
    Pose Base;
    Base.setWeaponAngle(1.0f);
    Pose Top;
    layerPose(Base, Top);
    CHECK(Base.WeaponAngle == Approx(1.0f));
    Top.setWeaponAngle(0.2f);
    layerPose(Base, Top);
    CHECK(Base.WeaponAngle == Approx(0.2f));

    Pose From;
    From.setWeaponAngle(0.0f);
    Pose To;
    To.setWeaponAngle(1.0f);
    CHECK(blendPoses(From, To, 0.25f).WeaponAngle == Approx(0.25f));
    // Only one of the poses sets it: that one's value.
    CHECK(blendPoses(Pose{}, To, 0.25f).WeaponAngle == Approx(1.0f));
    CHECK(blendPoses(From, Pose{}, 0.25f).HasWeapon);
}

TEST_CASE("parseClip: the WeaponOff key is the other wrist, checked like Weapon", "[anim]") {
    const Clip Parsed = parseClip(R"({
        "duration": 1.0,
        "keys": [
            { "t": 0.0, "pose": { "ForearmL": 0, "Weapon": 0, "WeaponOff": -20 } },
            { "t": 0.5, "pose": { "ForearmL": 90, "Weapon": 80, "WeaponOff": 20 } }
        ]
    })", "dual");
    const Pose Half = sampleClip(Parsed, 0.25f);
    REQUIRE(Half.HasWeaponOff);
    CHECK(Half.WeaponOffAngle == Approx(0.0f).margin(1e-6));
    CHECK(Half.WeaponAngle == Approx(40.0f * RadiansPerDegree));
    CHECK(Half.Mask.count() == 1);
    CHECK_FALSE(sampleClip(parseClip(OneShotJson, "plain"), 0.25f).HasWeaponOff);
    CHECK_THROWS_AS(parseClip(R"({
        "duration": 1.0,
        "keys": [
            { "t": 0.0, "pose": { "ForearmL": 0, "WeaponOff": 0 } },
            { "t": 0.5, "pose": { "ForearmL": 90 } }
        ]
    })", "uneven_off"), std::runtime_error);
    CHECK_THROWS_AS(parseClip(R"({
        "duration": 1.0,
        "keys": [ { "t": 0.0, "pose": { "WeaponOff": -190 } } ]
    })", "too_far_off"), std::runtime_error);
}

TEST_CASE("layerPose and blendPoses: the other wrist layers and blends on its own", "[anim]") {
    Pose Base;
    Base.setWeaponAngle(1.0f);
    Base.setWeaponOffAngle(-1.0f);
    Pose Top;
    Top.setWeaponOffAngle(0.5f);
    layerPose(Base, Top);
    CHECK(Base.WeaponAngle == Approx(1.0f));
    CHECK(Base.WeaponOffAngle == Approx(0.5f));

    Pose From;
    From.setWeaponOffAngle(0.0f);
    Pose To;
    To.setWeaponOffAngle(1.0f);
    const Pose Blended = blendPoses(From, To, 0.25f);
    CHECK(Blended.WeaponOffAngle == Approx(0.25f));
    CHECK_FALSE(Blended.HasWeapon);
    CHECK(blendPoses(Pose{}, To, 0.25f).WeaponOffAngle == Approx(1.0f));
}
