#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "anim/clip.hpp"
#include "core/text_file.hpp"
#include "editor/clip_writer.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

const std::filesystem::path PosesDir = std::filesystem::path(FIGHTER_DATA_DIR) / "poses";
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

/// Files that are laid out by hand, not as the writer lays them out: the
/// columns of a pose are padded to line up, or a long pose is wrapped over
/// several lines. The writer gives the same data on one line per key.
constexpr std::array<std::string_view, 18> HandLaidOut = {
    "block_high.json",  "block_low.json",   "block_mid.json",         "crouch.json",       "crouch_walk.json",
    "flinch.json",      "heavy_punch.json", "heavy_punch_close.json", "jab.json",          "jab_close.json",
    "kick.json",        "knockback.json",   "stagger.json",           "stance.json",       "stance_hammer.json",
    "stance_shield.json", "stance_sword.json", "walk.json",
};

/// Files whose joints are in another order than the writer's: a weapon in
/// the right hand with no striking phase to tell it (the right arm first), or
/// the wrist after the legs.
constexpr std::array<std::string_view, 5> OtherJointOrder = {
    "block_high_greatsword.json", "block_low_hammer.json", "block_low_sword.json", "block_mid_greatsword.json",
    "stance_greatsword.json",
};

std::vector<std::filesystem::path> getPoseFiles() {
    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(PosesDir)) {
        if (Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    std::ranges::sort(Files);
    return Files;
}

template <size_t Size>
bool isListed(const std::array<std::string_view, Size>& Names, const std::filesystem::path& File) {
    return std::ranges::find(Names, File.filename().string()) != Names.end();
}

std::string withoutSpaces(const std::string& Text) {
    std::string Result;
    for (const char Letter : Text) {
        if (!std::isspace(static_cast<unsigned char>(Letter))) Result += Letter;
    }
    return Result;
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

TEST_CASE("Every clip of data/poses is written back as it was or as the same data", "[editor]") {
    int Identical = 0;
    for (const auto& File : getPoseFiles()) {
        INFO(File.filename().string());
        const std::string Original = readTextFile(File);
        const anim::Clip Loaded = anim::loadClip(File);
        const std::string Written = writeClip(Loaded);

        if (isListed(HandLaidOut, File)) {
            CHECK(Written != Original);
            CHECK(withoutSpaces(Written) == withoutSpaces(Original));
        } else if (isListed(OtherJointOrder, File)) {
            CHECK(Written != Original);
        } else {
            CHECK(Written == Original);
            ++Identical;
        }
        // Whatever the layout, the data is the same and writing is stable.
        const anim::Clip Reloaded = anim::parseClip(Written, Loaded.Name);
        checkSameClip(Loaded, Reloaded);
        CHECK(writeClip(Reloaded) == Written);
    }
    CHECK(Identical > 0);
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
    const std::filesystem::path Dir = std::filesystem::temp_directory_path() / "fighter_test_save_clip";
    std::filesystem::create_directories(Dir);
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
    std::filesystem::remove_all(Dir);
}
