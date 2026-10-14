#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "ai/ai_controller.hpp"
#include "combat/battle.hpp"
#include "combat/moveset.hpp"

using namespace fighter;
using namespace fighter::ai;
using namespace fighter::combat;

// Scenario tests with a real Battle and no window: the AI plays the right
// fighter against a left one that stands still or follows a script.

namespace {

constexpr double Dt = 1.0 / 60.0;

BattleConfig makeConfig() {
    BattleConfig Config;
    Config.DataDir = FIGHTER_DATA_DIR;
    return Config;
}

/// The AI of the right fighter, with the unarmed repertoire of the data.
std::unique_ptr<AiController> makeAi(const AiConfig& Config) {
    const MoveLibrary Library = MoveLibrary::load(FIGHTER_DATA_DIR);
    return std::make_unique<AiController>(
        1, Config, buildRepertoire(Library, *Library.findSet(UnarmedMoveSetId), Config));
}

float getDistance(const RenderSnapshot& State) {
    return std::abs(State.Fighters[1].Position.X - State.Fighters[0].Position.X);
}

bool isSameState(const RenderSnapshot& First, const RenderSnapshot& Second) {
    for (size_t Index = 0; Index < First.Fighters.size(); ++Index) {
        const FighterView& A = First.Fighters[Index];
        const FighterView& B = Second.Fighters[Index];
        if (A.Position.X != B.Position.X || A.Position.Y != B.Position.Y || A.Hp != B.Hp || A.Stamina != B.Stamina ||
            A.State != B.State || A.MoveId != B.MoveId) {
            return false;
        }
    }
    return First.Tick == Second.Tick;
}

} // namespace

TEST_CASE("AI fight: the AI walks up to a dummy and lands a strike", "[ai][fight]") {
    Battle Fight(makeConfig());
    const float StartDistance = getDistance(Fight.getSnapshot());
    AiConfig Config;
    Config.AttackChance = 1.0f;
    const auto Ai = makeAi(Config);
    bool Closed = false;
    for (int Tick = 0; Tick < 20 * 60 && Fight.getSnapshot().Fighters[0].Hp >= Fight.getSnapshot().Fighters[0].MaxHp; ++Tick) {
        const PlayerCommands Cmd = Ai->getCommands(Fight.getSnapshot(), Dt);
        Fight.update(PlayerCommands{}, Cmd, Dt);
        if (getDistance(Fight.getSnapshot()) < StartDistance - 0.5f) Closed = true;
    }
    CHECK(Closed);
    CHECK(Fight.getSnapshot().Fighters[0].Hp < Fight.getSnapshot().Fighters[0].MaxHp);
}

TEST_CASE("AI fight: the same seed gives the same fight", "[ai][fight]") {
    AiConfig Config;
    Config.Seed = 5;
    Battle First(makeConfig());
    Battle Second(makeConfig());
    const auto FirstAi = makeAi(Config);
    const auto SecondAi = makeAi(Config);
    for (int Tick = 0; Tick < 6 * 60; ++Tick) {
        const PlayerCommands FirstCmd = FirstAi->getCommands(First.getSnapshot(), Dt);
        const PlayerCommands SecondCmd = SecondAi->getCommands(Second.getSnapshot(), Dt);
        REQUIRE(FirstCmd == SecondCmd);
        First.update(PlayerCommands{}, FirstCmd, Dt);
        Second.update(PlayerCommands{}, SecondCmd, Dt);
        REQUIRE(isSameState(First.getSnapshot(), Second.getSnapshot()));
    }
}

TEST_CASE("AI fight: a replay of the recorded commands reproduces the fight", "[ai][fight]") {
    AiConfig Config;
    Config.Seed = 9;
    Config.AttackChance = 0.9f;
    constexpr int Ticks = 8 * 60;

    Battle Original(makeConfig());
    const auto Ai = makeAi(Config);
    RecordingController Recorder(*Ai);
    std::vector<RenderSnapshot> Played;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        const PlayerCommands Cmd = Recorder.getCommands(Original.getSnapshot(), Dt);
        Original.update(PlayerCommands{}, Cmd, Dt);
        Played.push_back(Original.getSnapshot());
    }
    REQUIRE(Recorder.getRecord().size() == static_cast<size_t>(Ticks));

    Battle Copy(makeConfig());
    ReplayController Replay(Recorder.getRecord());
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        Copy.update(PlayerCommands{}, Replay.getCommands(Copy.getSnapshot(), Dt), Dt);
        REQUIRE(isSameState(Played[static_cast<size_t>(Tick)], Copy.getSnapshot()));
    }
    CHECK(Replay.isFinished());
}

TEST_CASE("AI fight: the AI raises a block against a strike when the chance is 1", "[ai][fight]") {
    AiConfig Config;
    Config.BlockChance = 1.0f;
    Config.AttackChance = 0.0f;
    Config.ReactionDelaySec = 0.0f;
    Battle Fight(makeConfig());
    const auto Ai = makeAi(Config);
    bool Blocked = false;
    for (int Tick = 0; Tick < 15 * 60; ++Tick) {
        const RenderSnapshot& State = Fight.getSnapshot();
        // The left fighter walks up and jabs again and again.
        PlayerCommands Left;
        if (getDistance(State) > 0.8f) Left.MoveX = 1.0f;
        else Left.Light = Tick % 40 < 6;
        Fight.update(Left, Ai->getCommands(State, Dt), Dt);
        if (Fight.getSnapshot().Fighters[1].State == FighterState::Blocking) Blocked = true;
    }
    CHECK(Blocked);
}
