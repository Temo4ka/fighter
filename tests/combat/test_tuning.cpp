#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "combat/tuning.hpp"

using namespace fighter::combat;

TEST_CASE("parseCombatTuning: values, defaults and errors", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "hitSpeedThreshold": 1.5 })");
    CHECK(Tuning.HitSpeedThreshold == 1.5f);
    CHECK(Tuning.SpawnDistance == CombatTuning{}.SpawnDistance);

    CHECK_THROWS_AS(parseCombatTuning("[1, 2]"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "hitSpeed": 1.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "spawnDistance": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "bodyHalfWidth": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "separationSpeed": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "exhaustedSpeedScale": 1.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "exhaustedRecoverFraction": -0.1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "chainWindowSec": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "maxChainLength": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "maxChainLength": 2.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blockWalkSpeedScale": 2 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "endSettleSec": -1 })"), std::runtime_error);
}

TEST_CASE("parseCombatTuning: stamina, chain and block parameters", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "exhaustedRecoverFraction": 0.5, "chainWindowSec": 0.2,
        "maxChainLength": 4, "blockWalkSpeedScale": 0.3, "endSettleSec": 2 })");
    CHECK(Tuning.ExhaustedRecoverFraction == 0.5f);
    CHECK(Tuning.ChainWindowSec == 0.2f);
    CHECK(Tuning.MaxChainLength == 4);
    CHECK(Tuning.BlockWalkSpeedScale == 0.3f);
    CHECK(Tuning.EndSettleSec == 2.0f);
}

TEST_CASE("loadCombatTuning: data/combat.json loads", "[combat]") {
    const CombatTuning Tuning = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json");
    CHECK(Tuning.SpawnDistance > 0.0f);
    CHECK(Tuning.HitSpeedThreshold > 0.0f);
    CHECK(Tuning.BodyHalfWidth > 0.0f);
    CHECK(Tuning.SeparationSpeed > 0.0f);
}
