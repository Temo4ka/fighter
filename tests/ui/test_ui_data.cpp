#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "ui/fighter_list.hpp"
#include "ui/results_text.hpp"
#include "ui/ui_config.hpp"

using namespace fighter;
using namespace fighter::ui;

namespace {

std::filesystem::path makeTempDir(const std::string& Name) {
    const std::filesystem::path Dir = std::filesystem::temp_directory_path() / ("fighter_ui_test_" + Name);
    std::filesystem::remove_all(Dir);
    std::filesystem::create_directories(Dir);
    return Dir;
}

bool hasLine(const std::vector<std::string>& Lines, const std::string& Needle) {
    for (const std::string& Line : Lines) {
        if (Line.find(Needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("listFighters: json stems, sorted, other files ignored", "[ui][data]") {
    const std::filesystem::path Dir = makeTempDir("list");
    for (const char* Name : {"rogue.json", "knight.json", "notes.txt"}) std::ofstream(Dir / Name) << "{}";
    const std::vector<std::string> Names = listFighters(Dir);
    REQUIRE(Names.size() == 2);
    CHECK(Names[0] == "knight");
    CHECK(Names[1] == "rogue");
    std::filesystem::remove_all(Dir);
}

TEST_CASE("listFighters: a missing directory is empty", "[ui][data]") {
    CHECK(listFighters("/no/such/fighter/dir").empty());
}

TEST_CASE("listFighters: the shipped data has fighters", "[ui][data]") {
    CHECK(listFighters(std::filesystem::path(FIGHTER_SOURCE_DIR) / "data" / "fighters").size() >= 2);
}

TEST_CASE("parseUiConfig: reads the delay", "[ui][data]") {
    CHECK(parseUiConfig(R"({"results_delay_sec": 3.5})", "t").ResultsDelaySec == 3.5);
    CHECK(parseUiConfig("{}", "t").ResultsDelaySec == UiConfig{}.ResultsDelaySec);
}

TEST_CASE("parseUiConfig: errors name the source, key and value", "[ui][data]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(parseUiConfig(R"({"results_delay_sec": -1})", "ui.json"),
                      ContainsSubstring("ui.json") && ContainsSubstring("results_delay_sec") && ContainsSubstring("-1"));
    CHECK_THROWS_WITH(parseUiConfig(R"({"typo": 1})", "ui.json"), ContainsSubstring("typo"));
    CHECK_THROWS_AS(parseUiConfig("[1]", "ui.json"), std::runtime_error);
    CHECK_THROWS_AS(parseUiConfig("{", "ui.json"), std::runtime_error);
}

TEST_CASE("loadUiConfig: missing file gives the defaults, shipped file parses", "[ui][data]") {
    CHECK(loadUiConfig("/no/such/ui.json").ResultsDelaySec == UiConfig{}.ResultsDelaySec);
    CHECK_NOTHROW(loadUiConfig(std::filesystem::path(FIGHTER_SOURCE_DIR) / "data" / "ui.json"));
}

TEST_CASE("describeResult: winner, hits by part and strikes", "[ui][results]") {
    combat::BattleResult Result;
    Result.WinnerSide = combat::Winner::Right;
    Result.End = combat::BattleEnd::Knockout;
    Result.TimeSec = 12.34;
    Result.Fighters[0].Hp = 0.0f;
    Result.Fighters[0].HitsTaken[static_cast<size_t>(BodyPart::Head)] = {.Hits = 3, .Damage = 21.0f};
    Result.Fighters[1].Hp = 55.5f;
    Result.Fighters[1].Moves["jab"] = {.Thrown = 5, .Landed = 3, .Blocked = 1, .Damage = 21.0f};

    const ResultsText Text = describeResult(Result, {"Knight", "Rogue"});
    CHECK(Text.Headline == "Rogue wins");
    CHECK(Text.Detail == "Knockout after 12.3 s");
    CHECK(Text.Columns[0][0] == "Knight");
    CHECK(hasLine(Text.Columns[0], "Head: 3"));
    CHECK(hasLine(Text.Columns[0], "HP left: 0.0"));
    CHECK(hasLine(Text.Columns[1], "HP left: 55.5"));
    CHECK(hasLine(Text.Columns[1], "jab: 5/3/1"));
    CHECK(hasLine(Text.Columns[1], "none"));   // no hits taken
}

TEST_CASE("describeResult: a draw on time", "[ui][results]") {
    combat::BattleResult Result;
    Result.WinnerSide = combat::Winner::Draw;
    Result.End = combat::BattleEnd::TimeUp;
    const ResultsText Text = describeResult(Result, {"A", "B"});
    CHECK(Text.Headline == "Draw");
    CHECK(Text.Detail.starts_with("Time up"));
}
