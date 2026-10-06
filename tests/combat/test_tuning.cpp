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

TEST_CASE("parseCombatTuning: the stop of a posed strike at a contact", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(
        R"({ "contactStopDepth": 0.02, "contactHoldSec": 0.1, "contactRecoveryBlendSec": 0.2 })");
    CHECK(Tuning.ContactStopDepth == 0.02f);
    CHECK(Tuning.ContactHoldSec == 0.1f);
    CHECK(Tuning.ContactRecoveryBlendSec == 0.2f);
    CHECK(parseCombatTuning(R"({ "posedSeparationSpeed": 9 })").PosedSeparationSpeed == 9.0f);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "posedSeparationSpeed": 0 })"), std::runtime_error);
    // The stop acts in every phase: there is no switch for the startup any more.
    CHECK_THROWS_AS(parseCombatTuning(R"({ "contactStopInStartup": true })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "contactStopDepth": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "contactHoldSec": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "contactRecoveryBlendSec": -1 })"), std::runtime_error);
}

TEST_CASE("parseCombatTuning: movement polish parameters", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "walkStopRate": 4, "stepMinSpeed": 0.2,
        "restMinFootSpread": 0.3, "crouchWalkSpeedScale": 0.4, "crouchStandUpSec": 0.1 })");
    CHECK(Tuning.WalkStopRate == 4.0f);
    CHECK(Tuning.StepMinSpeed == 0.2f);
    CHECK(Tuning.RestMinFootSpread == 0.3f);
    CHECK(Tuning.CrouchWalkSpeedScale == 0.4f);
    CHECK(Tuning.CrouchStandUpSec == 0.1f);
    // The stop no longer slides the feet, and the switched stance is gone
    // (the layered walk).
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stopSlidesFeet": true })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "switchStepShare": 0.8 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "restMinFootSpread": -0.1 })"), std::runtime_error);

    CHECK_THROWS_AS(parseCombatTuning(R"({ "blockWalkSpeedScale": 0.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "walkStopRate": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stepMinSpeed": -1 })"), std::runtime_error);
    // The cross-over of the legs moved into the blend table.
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stanceSettleSec": 0.15 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "crouchWalkSpeedScale": 1.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "crouchStandUpSec": -0.1 })"), std::runtime_error);
}

TEST_CASE("parseCombatTuning: the steps into an action that needs the legs", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "legStep": { "minDistance": 0.02, "liftHeight": 0.08,
        "startupShare": 0.6, "sec": 0.2, "stanceAfterStop": "authored" } })");
    CHECK(Tuning.LegStep.MinDistance == 0.02f);
    CHECK(Tuning.LegStep.LiftHeight == 0.08f);
    CHECK(Tuning.LegStep.StartupShare == 0.6f);
    CHECK(Tuning.LegStep.Sec == 0.2f);
    CHECK(Tuning.LegStep.Stance == StanceAfterStop::Authored);
    // The user's decision: mirrored by default.
    CHECK(CombatTuning{}.LegStep.Stance == StanceAfterStop::Mirror);
    CHECK(parseCombatTuning(R"({ "legStep": { "stanceAfterStop": "mirror" } })").LegStep.Stance ==
          StanceAfterStop::Mirror);
    CHECK(getStanceAfterStopName(StanceAfterStop::Authored) == "authored");

    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "stanceAfterStop": "switched" } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "lift": 0.1 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "liftHeight": 0 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "startupShare": 0 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "startupShare": 1.5 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "sec": 0 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "minDistance": -1 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": 1 })"), std::runtime_error);
    const CombatTuning Rest = parseCombatTuning(R"({ "stopMaxCoast": 0.04,
        "legStep": { "restepDistance": 0.07, "restSec": 0.2 } })");
    CHECK(Rest.StopMaxCoast == 0.04f);
    CHECK(parseCombatTuning(R"({ "shortStepMinSpread": 0.2 })").ShortStepMinSpread == 0.2f);
    CHECK(parseCombatTuning(R"({ "stopResumeDistance": 0.2 })").StopResumeDistance == 0.2f);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stopResumeDistance": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "shortStepMinSpread": -1 })"), std::runtime_error);
    CHECK(Rest.LegStep.RestepDistance == 0.07f);
    CHECK(Rest.LegStep.RestSec == 0.2f);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "stopMaxCoast": -0.01 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "restepDistance": 0 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "legStep": { "restSec": 0 } })"), std::runtime_error);
}

TEST_CASE("loadCombatTuning: data/combat.json loads", "[combat]") {
    const CombatTuning Tuning = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json");
    CHECK(Tuning.SpawnDistance > 0.0f);
    CHECK(Tuning.HitSpeedThreshold > 0.0f);
    CHECK(Tuning.BodyHalfWidth > 0.0f);
    CHECK(Tuning.SeparationSpeed > 0.0f);
}

TEST_CASE("parseCombatTuning: physics steps, contacts and the allowed overlap", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "physicsSteps": 2, "physicsSubSteps": 3, "contactHertz": 90,
        "fighterFriction": 0.2, "armOverlapTolerance": 0.03, "overlapTolerance": 0.005 })");
    CHECK(Tuning.PhysicsSteps == 2);
    CHECK(Tuning.PhysicsSubSteps == 3);
    CHECK(Tuning.ContactHertz == 90.0f);
    CHECK(Tuning.FighterFriction == 0.2f);
    CHECK(Tuning.ArmOverlapTolerance == 0.03f);
    CHECK(Tuning.OverlapTolerance == 0.005f);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "physicsSteps": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "physicsSteps": 1.5 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "physicsSubSteps": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "contactHertz": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "fighterFriction": -0.1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "armOverlapTolerance": -0.01 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "overlapTolerance": -0.01 })"), std::runtime_error);
}

TEST_CASE("parseCombatTuning: the spacing push", "[combat]") {
    const CombatTuning Tuning =
        parseCombatTuning(R"({ "pushMaxSpeed": 2, "pushAcceleration": 30, "pushSoftOverlap": 0.005 })");
    CHECK(Tuning.PushMaxSpeed == 2.0f);
    CHECK(Tuning.PushAcceleration == 30.0f);
    CHECK(Tuning.PushSoftOverlap == 0.005f);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "pushMaxSpeed": 0 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "pushAcceleration": -1 })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "pushSoftOverlap": -0.1 })"), std::runtime_error);
}

TEST_CASE("BlendTable: the most specific rule gives the blend time", "[combat]") {
    const CombatTuning Tuning = parseCombatTuning(R"({ "blends": { "default": 0.1, "strikeStartupShare": 0.4,
        "rules": [
            { "from": "any", "to": "strike", "sec": 0.03 },
            { "from": "walk", "to": "stance", "sec": 0.15 },
            { "from": "reaction", "to": "any", "sec": 0.2 },
            { "from": "block", "to": "strike", "sec": 0.05 } ] } })");
    const BlendTable& Blends = Tuning.Blends;
    CHECK(Blends.StrikeStartupShare == 0.4f);
    CHECK(Blends.getSec(PoseKind::Walk, PoseKind::Stance) == 0.15f);
    CHECK(Blends.getSec(PoseKind::Stance, PoseKind::Walk) == 0.1f);    // no rule: the default
    CHECK(Blends.getSec(PoseKind::Walk, PoseKind::Strike) == 0.03f);   // any -> strike
    CHECK(Blends.getSec(PoseKind::Block, PoseKind::Strike) == 0.05f);  // both kinds win
    CHECK(Blends.getSec(PoseKind::Reaction, PoseKind::Stance) == 0.2f);
    // The kind blended into is more specific than the one blended from.
    CHECK(Blends.getSec(PoseKind::Reaction, PoseKind::Strike) == 0.03f);
    CHECK(getPoseKindName(PoseKind::CrouchWalk) == "crouchWalk");

    CHECK_THROWS_AS(parseCombatTuning(R"({ "blends": { "rules": [{ "from": "run", "to": "any", "sec": 1 }] } })"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blends": { "rules": [{ "from": "walk", "to": "any", "sec": -1 }] } })"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blends": { "rules": [{ "from": "walk", "to": "any", "s": 1 }] } })"),
                    std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blends": { "fallback": 0.1 } })"), std::runtime_error);
    CHECK_THROWS_AS(parseCombatTuning(R"({ "blends": { "strikeStartupShare": 2 } })"), std::runtime_error);
}
