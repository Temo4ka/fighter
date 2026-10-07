#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "ui/fighter_card.hpp"
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

TEST_CASE("describeResult: winner, aligned rows for both fighters", "[ui][results]") {
    combat::BattleResult Result;
    Result.WinnerSide = combat::Winner::Right;
    Result.End = combat::BattleEnd::Knockout;
    Result.TimeSec = 12.34;
    Result.Fighters[0].HitsTaken[static_cast<size_t>(BodyPart::Head)] = {.Hits = 3, .Damage = 21.0f};
    Result.Fighters[1].Moves["jab"] = {.Thrown = 5, .Landed = 3, .Blocked = 1, .Damage = 21.0f};
    Result.Fighters[0].DamageDealt = 7.5f;

    const ResultsText Text = describeResult(Result, {"Knight", "Rogue"});
    CHECK(Text.Headline == "Rogue wins");
    CHECK(Text.Detail == "Knockout after 12.3 s");

    const auto Find = [&](const std::string& Label) -> const ResultsRow* {
        for (const ResultsRow& Row : Text.Rows) {
            if (!Row.IsHeader && Row.Label == Label) return &Row;
        }
        return nullptr;
    };
    const ResultsRow* Dealt = Find("Dealt");
    REQUIRE(Dealt);
    CHECK(Dealt->Cells[0] == "7.5");
    const ResultsRow* Head = Find("Head");
    REQUIRE(Head);
    CHECK(Head->Cells[0] == "3 (21.0)");
    CHECK(Head->Cells[1] == "-");
    CHECK(Find("Torso") == nullptr);   // not hit on either side
    const ResultsRow* Jab = Find("jab");
    REQUIRE(Jab);
    CHECK(Jab->Cells[0] == "-");
    CHECK(Jab->Cells[1] == "5/3/1");
}

TEST_CASE("describeResult: a draw on time", "[ui][results]") {
    combat::BattleResult Result;
    Result.WinnerSide = combat::Winner::Draw;
    Result.End = combat::BattleEnd::TimeUp;
    const ResultsText Text = describeResult(Result, {"A", "B"});
    CHECK(Text.Headline == "Draw");
    CHECK(Text.Detail.starts_with("Time up"));
}

TEST_CASE("parseUiConfig: palette, type scale and timings", "[ui][data]") {
    const UiConfig Config = parseUiConfig(R"({
        "highlight_sec": 0.2, "dim_alpha": 99,
        "colors": {"accent": "#102030", "panel": "#aabbccdd"},
        "type_scale": {"title": 0.2}
    })", "t");
    CHECK(Config.HighlightSec == 0.2);
    CHECK(Config.DimAlpha == 99);
    CHECK(Config.Colors.Accent == UiColor{0x10, 0x20, 0x30, 255});
    CHECK(Config.Colors.Panel == UiColor{0xaa, 0xbb, 0xcc, 0xdd});
    CHECK(Config.Type.Title == 0.2f);
    CHECK(Config.Type.Body == UiTypeScale{}.Body);
}

TEST_CASE("parseUiConfig: bad colors and sizes name the key", "[ui][data]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(parseUiConfig(R"({"colors": {"accent": "red"}})", "ui.json"),
                      ContainsSubstring("colors.accent") && ContainsSubstring("red"));
    CHECK_THROWS_WITH(parseUiConfig(R"({"colors": {"glow": "#000000"}})", "ui.json"), ContainsSubstring("colors.glow"));
    CHECK_THROWS_WITH(parseUiConfig(R"({"type_scale": {"body": 5}})", "ui.json"), ContainsSubstring("type_scale.body"));
    CHECK_THROWS_WITH(parseUiConfig(R"({"dim_alpha": 300})", "ui.json"), ContainsSubstring("dim_alpha"));
}

TEST_CASE("parseUiColor: accepts only hex forms", "[ui][data]") {
    UiColor Color;
    CHECK(parseUiColor("#FFc800", Color));
    CHECK(Color == UiColor{255, 200, 0, 255});
    CHECK_FALSE(parseUiColor("ffc800", Color));
    CHECK_FALSE(parseUiColor("#ffc80", Color));
    CHECK_FALSE(parseUiColor("#gggggg", Color));
}

TEST_CASE("easeToward: approaches without overshoot, zero time jumps", "[ui][data]") {
    CHECK(easeToward(0.0f, 1.0f, 0.1, 0.0) == 1.0f);
    float Position = 0.0f;
    for (int Step = 0; Step < 20; ++Step) {
        const float Next = easeToward(Position, 1.0f, 0.016, 0.06);
        CHECK(Next >= Position);
        CHECK(Next <= 1.0f);
        Position = Next;
    }
    CHECK(Position > 0.9f);
    CHECK(easeToward(0.5f, 1.0f, 0.0, 0.06) == 0.5f);
}

TEST_CASE("loadFighterCard: shipped fighter shows name, weapon and stats", "[ui][data]") {
    const FighterCard Card = loadFighterCard(std::filesystem::path(FIGHTER_SOURCE_DIR) / "data", "knight");
    CHECK(Card.Error.empty());
    CHECK(Card.Name == "Knight");
    CHECK(Card.Strength == 14);
    CHECK(Card.Weapon != "Unarmed");
}

TEST_CASE("loadFighterCard: a missing sheet gives an error card", "[ui][data]") {
    const FighterCard Card = loadFighterCard(std::filesystem::path(FIGHTER_SOURCE_DIR) / "data", "nobody");
    CHECK_FALSE(Card.Error.empty());
    CHECK(Card.Name == "nobody");
}
