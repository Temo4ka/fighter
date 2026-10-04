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
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blockBackSpeedScale": 2 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "endSettleSec": -1 })"), std::runtime_error);
}

TEST_CASE("parseCombatTuning: stamina, chain and block parameters", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "exhaustedRecoverFraction": 0.5, "chainWindowSec": 0.2,
        "maxChainLength": 4, "blockBackSpeedScale": 0.25, "endSettleSec": 2 })");
    CHECK(Tuning.ExhaustedRecoverFraction == 0.5f);
    CHECK(Tuning.ChainWindowSec == 0.2f);
    CHECK(Tuning.MaxChainLength == 4);
    CHECK(Tuning.BlockBackSpeedScale == 0.25f);
    CHECK(Tuning.EndSettleSec == 2.0f);
}

TEST_CASE("parseCombatTuning: movement polish parameters", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "walkStopRate": 4, "stanceSettleSec": 0.2,
        "switchStepShare": 0.5, "crouchWalkSpeedScale": 0.4, "crouchStandUpSec": 0.1 })");
    CHECK(Tuning.WalkStopRate == 4.0f);
    CHECK(Tuning.StanceSettleSec == 0.2f);
    CHECK(Tuning.SwitchStepShare == 0.5f);
    CHECK(Tuning.CrouchWalkSpeedScale == 0.4f);
    CHECK(Tuning.CrouchStandUpSec == 0.1f);

    CHECK_THROWS_AS(parseCombatTuning(R"({ "blockWalkSpeedScale": 0.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "walkStopRate": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stanceSettleSec": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "switchStepShare": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "crouchWalkSpeedScale": 1.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "crouchStandUpSec": -0.1 })"), std::runtime_error);
}

TEST_CASE("loadCombatTuning: data/combat.json loads", "[combat]") {
    const CombatTuning Tuning = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json");
    CHECK(Tuning.SpawnDistance > 0.0f);
    CHECK(Tuning.HitSpeedThreshold > 0.0f);
    CHECK(Tuning.BodyHalfWidth > 0.0f);
    CHECK(Tuning.SeparationSpeed > 0.0f);
}
