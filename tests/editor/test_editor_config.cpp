#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

#include "editor/editor_config.hpp"

using namespace fighter::editor;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

} // namespace

TEST_CASE("parseEditorConfig: every key is optional", "[editor][config]") {
    const EditorConfig Defaults;
    CHECK(parseEditorConfig("{}").DivergenceDeg == Defaults.DivergenceDeg);

    const EditorConfig Config = parseEditorConfig(
        R"({"tip_floor_margin_m": 0.1, "limit_margin_deg": 3, "divergence_deg": 20, "trajectory_step_sec": 0.05})");
    CHECK(Config.TipFloorMarginM == 0.1f);
    CHECK(Config.LimitMarginDeg == 3.0f);
    CHECK(Config.DivergenceDeg == 20.0f);
    CHECK(Config.TrajectoryStepSec == 0.05f);
}

TEST_CASE("parseEditorConfig: errors name the field", "[editor][config]") {
    CHECK_THROWS_WITH(parseEditorConfig(R"({"divergence_dg": 1})"), ContainsSubstring("unknown field 'divergence_dg'"));
    CHECK_THROWS_WITH(parseEditorConfig(R"({"divergence_deg": 500})"), ContainsSubstring("field 'divergence_deg': 500 is out of"));
    CHECK_THROWS_WITH(parseEditorConfig(R"({"limit_margin_deg": "a"})"), ContainsSubstring("field 'limit_margin_deg': must be a number"));
    CHECK_THROWS_WITH(parseEditorConfig("[]"), ContainsSubstring("JSON object"));
}

TEST_CASE("loadEditorConfig: an error names the file; the real file is sound", "[editor][config]") {
    CHECK_THROWS_WITH(loadEditorConfig(DataDir / "no_such_editor.json"), ContainsSubstring("no_such_editor.json"));
    const EditorConfig Real = loadEditorConfig(DataDir / "editor.json");
    CHECK(Real.DivergenceDeg > 0.0f);
    CHECK(loadEditorConfigOrDefault(DataDir / "poses").DivergenceDeg == EditorConfig{}.DivergenceDeg);
}

TEST_CASE("loadEditorConfig: an unknown key of a file names the file and the key", "[editor][config]") {
    const std::filesystem::path Dir = std::filesystem::temp_directory_path() / "fighter_editor_config_test";
    std::filesystem::create_directories(Dir);
    const std::filesystem::path File = Dir / "editor.json";
    {
        std::ofstream Out(File);
        Out << R"({"divergence_deg": 20, "bogus_key": 1})";
    }
    CHECK_THROWS_WITH(loadEditorConfig(File), ContainsSubstring("editor.json") && ContainsSubstring("unknown field 'bogus_key'"));
    CHECK_THROWS_WITH(loadEditorConfigOrDefault(Dir), ContainsSubstring("bogus_key"));
    std::filesystem::remove_all(Dir);
}
