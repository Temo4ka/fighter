#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

#include "combat/stand_config.hpp"

using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("parseStandConfig: every key is optional", "[combat][stand]") {
    const StandConfig Defaults;
    const StandConfig Empty = parseStandConfig("{}");
    CHECK(Empty.DummyDistanceM == Defaults.DummyDistanceM);
    CHECK(Empty.Attacker.Strength == Defaults.Attacker.Strength);

    const StandConfig Config = parseStandConfig(R"({"attacker": {"strength": 20}, "dummy": {"constitution": 3},
        "dummy_distance_m": 1.1, "use_move_range": false, "settle_sec": 0.2, "start_timeout_sec": 2,
        "move_timeout_sec": 6, "repeat_pause_sec": 0.1, "no_dummy_distance_m": 9})");
    CHECK(Config.Attacker.Strength == 20);
    CHECK(Config.Attacker.Dexterity == 10);
    CHECK(Config.Dummy.Constitution == 3);
    CHECK(Config.DummyDistanceM == 1.1f);
    CHECK_FALSE(Config.UseMoveRange);
    CHECK(Config.SettleSec == 0.2f);
    CHECK(Config.StartTimeoutSec == 2.0f);
    CHECK(Config.MoveTimeoutSec == 6.0f);
    CHECK(Config.RepeatPauseSec == 0.1f);
    CHECK(Config.NoDummyDistanceM == 9.0f);
}

TEST_CASE("parseStandConfig: errors name the field and the value", "[combat][stand]") {
    CHECK_THROWS_WITH(parseStandConfig(R"({"dummy_distanc_m": 1})"), ContainsSubstring("unknown field 'dummy_distanc_m'"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"settle_sec": -1})"), ContainsSubstring("field 'settle_sec': -1 is out of"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"settle_sec": "a"})"), ContainsSubstring("field 'settle_sec': must be a number"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"attacker": {"strong": 1}})"), ContainsSubstring("unknown field 'attacker.strong'"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"attacker": {"strength": 99}})"), ContainsSubstring("field 'attacker'"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"dummy": {"strength": 1.5}})"), ContainsSubstring("field 'dummy.strength': must be an integer"));
    CHECK_THROWS_WITH(parseStandConfig(R"({"use_move_range": 1})"), ContainsSubstring("field 'use_move_range'"));
    CHECK_THROWS_WITH(parseStandConfig("[]"), ContainsSubstring("JSON object"));
    CHECK_THROWS(parseStandConfig("{ nope"));
}

TEST_CASE("loadStandConfig: the sample file loads, a missing one names the path", "[combat][stand]") {
    const std::filesystem::path Sample = std::filesystem::path(FIGHTER_DATA_DIR) / "stand.json";
    const StandConfig Config = loadStandConfig(Sample);
    CHECK(Config.DummyDistanceM > 0.0f);
    CHECK_THROWS_WITH(loadStandConfig("no_such_dir/stand.json"), ContainsSubstring("no_such_dir/stand.json"));
}
