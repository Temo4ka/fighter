#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <stdexcept>

#include "ai/ai_config.hpp"

using namespace fighter::ai;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("parseAiConfig: an empty object gives the defaults", "[ai][config]") {
    const AiConfig Config = parseAiConfig("{}");
    const AiConfig Defaults;
    CHECK(Config.ReactionDelaySec == Defaults.ReactionDelaySec);
    CHECK(Config.BlockChance == Defaults.BlockChance);
    CHECK(Config.Seed == Defaults.Seed);
}

TEST_CASE("parseAiConfig: keys override the defaults", "[ai][config]") {
    const AiConfig Config = parseAiConfig(
        R"({"reactionDelaySec": 0.5, "blockChance": 1, "attackChance": 0.25,
            "defaultRangeM": [0.4, 1.2], "seed": 42})");
    CHECK(Config.ReactionDelaySec == Approx(0.5f));
    CHECK(Config.BlockChance == Approx(1.0f));
    CHECK(Config.AttackChance == Approx(0.25f));
    CHECK(Config.DefaultMinRangeM == Approx(0.4f));
    CHECK(Config.DefaultMaxRangeM == Approx(1.2f));
    CHECK(Config.Seed == 42);
}

TEST_CASE("parseAiConfig: errors name the key and the value", "[ai][config]") {
    CHECK_THROWS_WITH(parseAiConfig(R"({"blockChence": 1})"), ContainsSubstring("unknown parameter 'blockChence'"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"blockChance": 1.5})"), ContainsSubstring("blockChance"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"blockChance": 1.5})"), ContainsSubstring("1.5"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"recoverSec": "fast"})"), ContainsSubstring("recoverSec must be a number"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"defaultRangeM": [1.0, 0.5]})"), ContainsSubstring("defaultRangeM"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"defaultRangeM": 1})"), ContainsSubstring("defaultRangeM"));
    CHECK_THROWS_WITH(parseAiConfig(R"({"seed": -1})"), ContainsSubstring("seed"));
    CHECK_THROWS_WITH(parseAiConfig("[1]"), ContainsSubstring("JSON object"));
    CHECK_THROWS(parseAiConfig("{broken"));
}

TEST_CASE("loadAiConfig: the shipped data/ai.json is sound", "[ai][config][data]") {
    const std::filesystem::path Path = std::filesystem::path(FIGHTER_DATA_DIR) / "ai.json";
    CHECK_NOTHROW(loadAiConfig(Path));
}

TEST_CASE("loadAiConfig: a missing file names the path", "[ai][config]") {
    CHECK_THROWS_WITH(loadAiConfig("no/such/ai.json"), ContainsSubstring("no/such/ai.json"));
}
