#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "anim/clip.hpp"
#include "editor/clip_history.hpp"
#include "editor/clip_writer.hpp"
#include "editor/new_clip.hpp"

using namespace fighter;
using namespace fighter::editor;

namespace {

/// A scratch data directory with poses/taken.json; removed at the end.
struct ScratchData {
    std::filesystem::path Dir = std::filesystem::temp_directory_path() / "fighter_test_new_clip";

    ScratchData() {
        std::filesystem::remove_all(Dir);
        std::filesystem::create_directories(Dir / "poses");
        std::ofstream(Dir / "poses" / "taken.json") << "{}";
    }
    ~ScratchData() {
        std::error_code Error;
        std::filesystem::remove_all(Dir, Error);
    }
};

} // namespace

TEST_CASE("A new clip name is checked", "[editor][new_clip]") {
    const ScratchData Scratch;
    CHECK(findNewClipProblem(Scratch.Dir, "sword_combo_2").empty());
    CHECK(findNewClipProblem(Scratch.Dir, "a").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "Upper").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "2fast").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "_lead").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "has space").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "dir/name").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "..").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "name.json").empty());
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, std::string(MaxClipNameLength + 1, 'a')).empty());
    CHECK(findNewClipProblem(Scratch.Dir, std::string(MaxClipNameLength, 'a')).empty());
}

TEST_CASE("An existing clip name is refused", "[editor][new_clip]") {
    const ScratchData Scratch;
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "taken").empty());
    CHECK(findNewClipProblem(Scratch.Dir, "free").empty());
}

TEST_CASE("A clip from a template has its content under the new name", "[editor][new_clip]") {
    const anim::Clip Template = anim::parseClip(R"({
      "duration": 0.6, "loop": false, "active": [0.2, 0.4], "strikers": ["ForearmR"], "stiffness": 1.5,
      "keys": [ { "t": 0, "pose": { "ForearmR": 40, "Torso": 3 } }, { "t": 0.3, "pose": { "ForearmR": 80, "Torso": 4 } } ] })",
                                                "stance_sword");
    const anim::Clip Made = makeClipFromTemplate(Template, "sword_new");
    CHECK(Made.Name == "sword_new");
    CHECK(Template.Name == "stance_sword");
    anim::Clip Same = Made;
    Same.Name = Template.Name;
    CHECK(isSameClip(Same, Template));
    CHECK(findClipProblem(Made).empty());
}

TEST_CASE("A new clip is written only by saving it", "[editor][new_clip]") {
    const ScratchData Scratch;
    const anim::Clip Template = anim::parseClip(R"({ "duration": 0.5, "keys": [ { "t": 0, "pose": { "Torso": 1 } } ] })", "t");
    const anim::Clip Made = makeClipFromTemplate(Template, "fresh");
    const std::filesystem::path Path = Scratch.Dir / "poses" / "fresh.json";
    CHECK_FALSE(std::filesystem::exists(Path));
    saveClip(Made, Path);
    CHECK(std::filesystem::exists(Path));
    CHECK_FALSE(findNewClipProblem(Scratch.Dir, "fresh").empty());
    const anim::Clip Loaded = anim::loadClip(Path);
    CHECK(Loaded.Keys.size() == 1);
}
