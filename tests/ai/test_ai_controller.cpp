#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ai/ai_controller.hpp"

using namespace fighter;
using namespace fighter::ai;
using namespace fighter::combat;

namespace {

constexpr double Dt = 1.0 / 60.0;

/// A jab for 0.6 - 0.9 m, a high one; the opponent's sweep is a low one.
Repertoire makeRepertoire() {
    Repertoire Moves;
    Moves.Attacks.push_back({.MoveId = "jab",
                             .Input = {InputDirection::Neutral, ButtonSet(AttackButton::Light)},
                             .MinRangeM = 0.6f,
                             .MaxRangeM = 0.9f,
                             .Role = "poke",
                             .Weight = 1.0f});
    Moves.Heights = {{"jab", "high"}, {"kick", "mid"}, {"sweep", "low"}};
    return Moves;
}

/// Quick and certain: the tests of one behaviour do not depend on chances.
AiConfig makeSure() {
    AiConfig Config;
    Config.AttackChance = 1.0f;
    Config.BlockChance = 1.0f;
    Config.ReactionDelaySec = 0.1f;
    Config.DecisionIntervalSec = 0.1f;
    return Config;
}

/// Fighter 0 (the AI) on the left, fighter 1 \p DistanceM to its right.
RenderSnapshot makeState(float DistanceM) {
    RenderSnapshot State;
    State.Fighters[0].Position = {-DistanceM * 0.5f, 0.0f};
    State.Fighters[0].FacingRight = true;
    State.Fighters[1].Position = {DistanceM * 0.5f, 0.0f};
    State.Fighters[1].FacingRight = false;
    return State;
}

void makeOpponentStrike(RenderSnapshot& State, std::string MoveId, AttackPhase Phase = AttackPhase::Startup) {
    State.Fighters[1].State = FighterState::Attacking;
    State.Fighters[1].MoveId = std::move(MoveId);
    State.Fighters[1].Phase = Phase;
}

/// Asks the AI for \p Ticks steps on an unchanging state; returns the last commands.
PlayerCommands think(AiController& Ai, const RenderSnapshot& State, int Ticks) {
    PlayerCommands Last;
    for (int Tick = 0; Tick < Ticks; ++Tick) Last = Ai.getCommands(State, Dt);
    return Last;
}

} // namespace

TEST_CASE("getAiStateName: names every state", "[ai][controller]") {
    CHECK(getAiStateName(AiState::Approach) == "Approach");
    CHECK(getAiStateName(AiState::Hold) == "Hold");
    CHECK(getAiStateName(AiState::Attack) == "Attack");
    CHECK(getAiStateName(AiState::Block) == "Block");
    CHECK(getAiStateName(AiState::Recover) == "Recover");
}

TEST_CASE("AiController: approaches when far", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    const PlayerCommands Cmd = Ai.getCommands(makeState(3.0f), Dt);
    CHECK(Cmd.MoveX > MoveDeadZone);
    CHECK_FALSE(Cmd.Light);
    CHECK(Ai.getState() == AiState::Approach);
    CHECK(Ai.getName() == "ai");

    // The right fighter walks to the left, towards the opponent.
    AiController Right(1, makeSure(), makeRepertoire());
    CHECK(Right.getCommands(makeState(3.0f), Dt).MoveX < -MoveDeadZone);
}

TEST_CASE("AiController: attacks when in range", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    const RenderSnapshot State = makeState(0.75f);
    const PlayerCommands Cmd = Ai.getCommands(State, Dt);
    CHECK(Cmd.Light);
    CHECK(Ai.getState() == AiState::Attack);
    REQUIRE(Ai.getChosenMove());
    CHECK(Ai.getChosenMove()->MoveId == "jab");
}

TEST_CASE("AiController: releases the buttons after PressSec and recovers", "[ai][controller]") {
    AiConfig Config = makeSure();
    Config.PressSec = 0.1f;
    Config.RecoverSec = 0.2f;
    AiController Ai(0, Config, makeRepertoire());
    const RenderSnapshot State = makeState(0.75f);
    REQUIRE(Ai.getCommands(State, Dt).Light);
    PlayerCommands Cmd;
    int Pressed = 1;
    for (int Tick = 0; Tick < 30 && Ai.getState() == AiState::Attack; ++Tick) {
        Cmd = Ai.getCommands(State, Dt);
        if (Cmd.Light) ++Pressed;
    }
    CHECK(Pressed >= 5);
    CHECK(Pressed <= 8);
    CHECK(Ai.getState() == AiState::Recover);
    CHECK_FALSE(Cmd.Light);
}

TEST_CASE("AiController: backs off when too close for every strike", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    const PlayerCommands Cmd = Ai.getCommands(makeState(0.3f), Dt);
    CHECK(Cmd.MoveX < -MoveDeadZone);
    CHECK_FALSE(Cmd.Light);
    CHECK(Ai.getState() == AiState::Hold);
}

TEST_CASE("AiController: strikes when backed against a wall too close", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    RenderSnapshot State = makeState(0.3f);
    State.Fighters[0].AgainstWall = true;
    const PlayerCommands Cmd = think(Ai, State, 3);
    CHECK(Cmd.Light);
    CHECK(Ai.getState() == AiState::Attack);
}

TEST_CASE("AiController: never strikes with attack chance 0", "[ai][controller]") {
    AiConfig Config = makeSure();
    Config.AttackChance = 0.0f;
    AiController Ai(0, Config, makeRepertoire());
    const RenderSnapshot State = makeState(0.75f);
    for (int Tick = 0; Tick < 120; ++Tick) CHECK_FALSE(Ai.getCommands(State, Dt).Light);
    CHECK(Ai.getState() == AiState::Hold);
}

TEST_CASE("AiController: does not strike when out of stamina", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    State.Fighters[0].MaxStamina = 100.0f;
    State.Fighters[0].Stamina = 1.0f;
    for (int Tick = 0; Tick < 60; ++Tick) CHECK_FALSE(Ai.getCommands(State, Dt).Light);
}

TEST_CASE("AiController: blocks a high attack high when the chance is 1", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    makeOpponentStrike(State, "jab");
    const PlayerCommands Cmd = think(Ai, State, 10);   // past the reaction delay
    CHECK(Ai.getState() == AiState::Block);
    CHECK(Ai.isBlockPlanned());
    CHECK(getBlockZone(Cmd, true) == BlockZone::High);
}

TEST_CASE("AiController: blocks mid and low attacks by their height", "[ai][controller]") {
    {
        AiController Ai(0, makeSure(), makeRepertoire());
        RenderSnapshot State = makeState(0.75f);
        makeOpponentStrike(State, "kick");
        CHECK(getBlockZone(think(Ai, State, 10), true) == BlockZone::Mid);
    }
    {
        AiController Ai(0, makeSure(), makeRepertoire());
        RenderSnapshot State = makeState(0.75f);
        makeOpponentStrike(State, "sweep", AttackPhase::Active);
        CHECK(getBlockZone(think(Ai, State, 10), true) == BlockZone::Low);
    }
}

TEST_CASE("AiController: waits out the reaction delay before blocking", "[ai][controller]") {
    AiConfig Config = makeSure();
    Config.ReactionDelaySec = 0.5f;
    AiController Ai(0, Config, makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    makeOpponentStrike(State, "jab");
    CHECK_FALSE(think(Ai, State, 20).Block);   // 0.33 s
    CHECK(think(Ai, State, 20).Block);         // 0.67 s
}

TEST_CASE("AiController: does not block with chance 0", "[ai][controller]") {
    AiConfig Config = makeSure();
    Config.BlockChance = 0.0f;
    AiController Ai(0, Config, makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    makeOpponentStrike(State, "jab");
    for (int Tick = 0; Tick < 60; ++Tick) CHECK_FALSE(Ai.getCommands(State, Dt).Block);
}

TEST_CASE("AiController: does not block a strike from beyond BlockRangeM", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    RenderSnapshot State = makeState(3.0f);
    makeOpponentStrike(State, "jab");
    CHECK_FALSE(think(Ai, State, 20).Block);
}

TEST_CASE("AiController: lowers the block after the strike and the hold time", "[ai][controller]") {
    AiConfig Config = makeSure();
    Config.BlockHoldSec = 0.2f;
    AiController Ai(0, Config, makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    makeOpponentStrike(State, "jab");
    REQUIRE(think(Ai, State, 10).Block);
    State.Fighters[1].State = FighterState::Idle;
    State.Fighters[1].Phase = AttackPhase::None;
    CHECK(think(Ai, State, 3).Block);          // still held
    CHECK_FALSE(think(Ai, State, 20).Block);   // released
    CHECK(Ai.getState() == AiState::Recover);
}

TEST_CASE("AiController: gives no input while hit or down", "[ai][controller]") {
    AiController Ai(0, makeSure(), makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    State.Fighters[0].State = FighterState::Reacting;
    CHECK(think(Ai, State, 5) == PlayerCommands{});
    CHECK(Ai.getState() == AiState::Recover);
    State.Fighters[0].State = FighterState::KnockedDown;
    CHECK(think(Ai, State, 5) == PlayerCommands{});
}

TEST_CASE("AiController: the same seed gives the same commands", "[ai][controller]") {
    AiConfig Config;   // the chances are real ones
    Config.Seed = 7;
    AiController First(0, Config, makeRepertoire());
    AiController Second(0, Config, makeRepertoire());
    RenderSnapshot State = makeState(0.75f);
    for (int Tick = 0; Tick < 600; ++Tick) {
        if (Tick % 90 == 40) makeOpponentStrike(State, "jab");
        if (Tick % 90 == 70) State.Fighters[1].State = FighterState::Idle;
        CHECK(First.getCommands(State, Dt) == Second.getCommands(State, Dt));
        CHECK(First.getState() == Second.getState());
    }
}

TEST_CASE("AiController: rollChance stays in [0, 1) and depends on the seed", "[ai][controller]") {
    AiConfig Config;
    Config.Seed = 3;
    AiController Ai(0, Config, makeRepertoire());
    float Sum = 0.0f;
    for (int Roll = 0; Roll < 1000; ++Roll) {
        const float Chance = Ai.rollChance();
        CHECK(Chance >= 0.0f);
        CHECK(Chance < 1.0f);
        Sum += Chance;
    }
    CHECK(Sum / 1000.0f > 0.4f);
    CHECK(Sum / 1000.0f < 0.6f);

    AiController Same(0, Config, makeRepertoire());
    Config.Seed = 4;
    AiController Other(0, Config, makeRepertoire());
    AiController Again(0, AiConfig{.Seed = 3}, makeRepertoire());
    CHECK(Same.rollChance() == Again.rollChance());
    CHECK(Same.rollChance() != Other.rollChance());
}

TEST_CASE("AiController: getConfig gives the config it plays with", "[ai][controller]") {
    AiConfig Config;
    Config.BlockChance = 0.75f;
    const AiController Ai(1, Config, makeRepertoire());
    CHECK(Ai.getConfig().BlockChance == 0.75f);
    CHECK(Ai.getChosenMove() == nullptr);
    CHECK_FALSE(Ai.isBlockPlanned());
}
