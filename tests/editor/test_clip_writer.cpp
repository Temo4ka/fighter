#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "anim/clip.hpp"
#include "core/text_file.hpp"
#include "editor/clip_writer.hpp"

#include "../scratch_dir.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

const std::filesystem::path PosesDir = std::filesystem::path(FIGHTER_DATA_DIR) / "poses";
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

std::vector<std::filesystem::path> getPoseFiles() {
    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(PosesDir)) {
        if (Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    std::ranges::sort(Files);
    return Files;
}

/// The same clip: every field, the keys with their angles within a hair.
void checkSameClip(const anim::Clip& Left, const anim::Clip& Right) {
    CHECK(Left.Loop == Right.Loop);
    CHECK(Left.DurationSec == Approx(Right.DurationSec));
    CHECK(Left.ActiveBeginSec == Approx(Right.ActiveBeginSec));
    CHECK(Left.ActiveEndSec == Approx(Right.ActiveEndSec));
    CHECK(Left.Strikers == Right.Strikers);
    CHECK(Left.Stiffness == Approx(Right.Stiffness));
    CHECK(Left.AllowMove == Right.AllowMove);
    CHECK(Left.BlendInSec.has_value() == Right.BlendInSec.has_value());
    CHECK(Left.BlendOutSec.has_value() == Right.BlendOutSec.has_value());
    REQUIRE(Left.PelvisTrack.size() == Right.PelvisTrack.size());
    for (size_t Index = 0; Index < Left.PelvisTrack.size(); ++Index) {
        CHECK(Left.PelvisTrack[Index].TimeSec == Approx(Right.PelvisTrack[Index].TimeSec));
        CHECK(Left.PelvisTrack[Index].OffsetX == Approx(Right.PelvisTrack[Index].OffsetX));
    }
    REQUIRE(Left.Keys.size() == Right.Keys.size());
    for (size_t Index = 0; Index < Left.Keys.size(); ++Index) {
        const anim::Pose& A = Left.Keys[Index].Target;
        const anim::Pose& B = Right.Keys[Index].Target;
        CHECK(Left.Keys[Index].TimeSec == Approx(Right.Keys[Index].TimeSec));
        CHECK(A.Mask == B.Mask);
        CHECK(A.HasWeapon == B.HasWeapon);
        CHECK(A.WeaponAngle == Approx(B.WeaponAngle).margin(1e-5));
        for (size_t Joint = 0; Joint < BodyPartCount; ++Joint) {
            CHECK(A.Angles[Joint] == Approx(B.Angles[Joint]).margin(1e-5));
        }
    }
}

anim::Clip makeClip() {
    return anim::parseClip(R"({
      "loop": false, "duration": 0.5, "active": [0.1, 0.2], "strikers": ["ForearmL"],
      "stiffness": 1.5, "allowMove": false, "blendIn": 0.04,
      "pelvisX": [ { "t": 0, "x": 0 }, { "t": 0.25, "x": 0.2 } ],
      "keys": [
        { "t": 0, "pose": { "Torso": -8, "ForearmL": 12.5, "Weapon": 85 } },
        { "t": 0.125, "pose": { "Torso": 0, "ForearmL": 100, "Weapon": -3.25 } }
      ] })",
                           "sample");
}

} // namespace

TEST_CASE("Every clip of data/poses is written back byte for byte", "[editor]") {
    for (const auto& File : getPoseFiles()) {
        INFO(File.filename().string());
        const anim::Clip Loaded = anim::loadClip(File);
        const std::string Written = writeClip(Loaded);
        CHECK(Written == readTextFile(File));
        checkSameClip(Loaded, anim::parseClip(Written, Loaded.Name));
    }
}

TEST_CASE("The writer prints whole angles without a decimal point", "[editor]") {
    const std::string Text = writeClip(makeClip());
    CHECK(Text.find("{ \"Torso\": -8, \"ForearmL\": 12.5, \"Weapon\": 85 }") != std::string::npos);
    CHECK(Text.find("\"Weapon\": -3.25") != std::string::npos);
}

TEST_CASE("The writer pads the times of the keys to the same decimals", "[editor]") {
    const std::string Text = writeClip(makeClip());
    CHECK(Text.find("{ \"t\": 0.000, \"pose\"") != std::string::npos);
    CHECK(Text.find("{ \"t\": 0.125, \"pose\"") != std::string::npos);
    CHECK(Text.find("{ \"t\": 0.00, \"x\": 0.0 }") != std::string::npos);
    CHECK(Text.find("{ \"t\": 0.25, \"x\": 0.2 }") != std::string::npos);
}

TEST_CASE("The writer leaves out what the clip does not have", "[editor]") {
    anim::Clip Plain = anim::parseClip(R"({"duration": 1.0, "keys": [{"t": 0, "pose": {"Torso": 5}}]})", "plain");
    const std::string Text = writeClip(Plain);
    CHECK(Text.find("active") == std::string::npos);
    CHECK(Text.find("strikers") == std::string::npos);
    CHECK(Text.find("stiffness") == std::string::npos);
    CHECK(Text.find("allowMove") == std::string::npos);
    CHECK(Text.find("blendIn") == std::string::npos);
    CHECK(Text.find("pelvisX") == std::string::npos);
    CHECK(Text.ends_with("}\n"));

    // A clip with one of the two set writes both.
    Plain.AllowMove = false;
    const std::string Both = writeClip(Plain);
    CHECK(Both.find("\"stiffness\": 1.0,") != std::string::npos);
    CHECK(Both.find("\"allowMove\": false,") != std::string::npos);
}

TEST_CASE("The writer keeps the fields in their order", "[editor]") {
    const std::string Text = writeClip(makeClip());
    size_t Position = 0;
    for (const std::string_view Field : {"\"loop\"", "\"duration\"", "\"active\"", "\"strikers\"", "\"stiffness\"",
                                         "\"allowMove\"", "\"blendIn\"", "\"pelvisX\"", "\"keys\""}) {
        const size_t Found = Text.find(Field);
        REQUIRE(Found != std::string::npos);
        CHECK(Found >= Position);
        Position = Found;
    }
}

TEST_CASE("The writer puts the right arm first when it strikes", "[editor]") {
    anim::Clip Right = anim::parseClip(
        R"({"duration": 1.0, "strikers": ["ForearmR"],
            "keys": [{"t": 0, "pose": {"UpperArmL": 1, "ForearmL": 2, "UpperArmR": 3, "ForearmR": 4}}]})",
        "right");
    CHECK(writeClip(Right).find("{ \"UpperArmR\": 3, \"ForearmR\": 4, \"UpperArmL\": 1, \"ForearmL\": 2 }") !=
          std::string::npos);
    Right.Strikers.reset();
    CHECK(writeClip(Right).find("{ \"UpperArmL\": 1, \"ForearmL\": 2, \"UpperArmR\": 3, \"ForearmR\": 4 }") !=
          std::string::npos);
}

TEST_CASE("An edited clip is written and read back", "[editor]") {
    anim::Clip Edited = makeClip();
    Edited.Keys[1].Target.setAngle(BodyPart::ForearmL, 33.3f * RadiansPerDegree);
    Edited.Keys[1].TimeSec = 0.1234f;
    const anim::Clip Back = anim::parseClip(writeClip(Edited), "back");
    CHECK(Back.Keys[1].Target.getAngle(BodyPart::ForearmL) == Approx(33.3f * RadiansPerDegree).margin(1e-5));
    CHECK(Back.Keys[1].TimeSec == Approx(0.1234f));
}

TEST_CASE("findClipProblem names what a data file would not accept", "[editor]") {
    anim::Clip Clip = makeClip();
    CHECK(findClipProblem(Clip).empty());
    Clip.Keys[1].TimeSec = 0.9f;   // after the end of the clip
    CHECK(findClipProblem(Clip).find("after the end") != std::string::npos);
}

TEST_CASE("saveClip writes the file, and nothing for a clip the loader would refuse", "[editor]") {
    const fighter::testutil::ScratchDir Scratch("save_clip");
    const std::filesystem::path& Dir = Scratch.getPath();
    const std::filesystem::path Path = Dir / "sample.json";

    anim::Clip Clip = makeClip();
    saveClip(Clip, Path);
    checkSameClip(anim::loadClip(Path), Clip);
    CHECK(readTextFile(Path) == writeClip(Clip));
    CHECK_FALSE(std::filesystem::exists(Dir / "sample.json.tmp"));

    const std::string Before = readTextFile(Path);
    Clip.Keys[1].TimeSec = 0.9f;
    CHECK_THROWS_AS(saveClip(Clip, Path), std::runtime_error);
    CHECK(readTextFile(Path) == Before);
}
