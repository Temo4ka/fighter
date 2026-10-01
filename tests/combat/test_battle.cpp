#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <ranges>

#include "combat/battle.hpp"

using namespace fighter::combat;
using Catch::Approx;

namespace {
constexpr double Dt = 1.0 / 60.0;

void run(Battle& Fight, const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, int Ticks) {
    for (int Tick = 0; Tick < Ticks; ++Tick) Fight.update(LeftCmd, RightCmd, Dt);
}
} // namespace

// Scenario tests of a fight without a window: scripted input -> expected state.
// For now they cover the phase 0 kinematic placeholder; agent D extends them
// once physics arrives.

TEST_CASE("Battle: fighters start on opposite sides facing each other", "[combat]") {
    Battle Fight(BattleConfig{});
    const auto& Snap = Fight.getSnapshot();
    CHECK(Snap.Fighters[0].Position.X < Snap.Fighters[1].Position.X);
    CHECK(Snap.Fighters[0].FacingRight);
    CHECK_FALSE(Snap.Fighters[1].FacingRight);
    CHECK(Snap.Fighters[0].Hp == Snap.Fighters[0].MaxHp);
}

TEST_CASE("Battle: walking stops at the arena wall", "[combat]") {
    Battle Fight(BattleConfig{});
    const float X0 = Fight.getSnapshot().Fighters[0].Position.X;
    run(Fight, {.MoveX = -1.0f}, {}, 30);
    CHECK(Fight.getSnapshot().Fighters[0].Position.X < X0);

    run(Fight, {.MoveX = -1.0f}, {}, 600);
    CHECK(Fight.getSnapshot().Fighters[0].Position.X > -Fight.getConfig().Arena.HalfWidthM);   // stayed inside
}

TEST_CASE("Battle: jump goes up and lands on the floor", "[combat]") {
    Battle Fight(BattleConfig{});
    run(Fight, {.Jump = true}, {}, 10);
    CHECK(Fight.getSnapshot().Fighters[0].Position.Y > 0.1f);
    run(Fight, {}, {}, 120);
    CHECK(Fight.getSnapshot().Fighters[0].Position.Y == Approx(0.0f));
}

TEST_CASE("Battle: result is available when round time runs out", "[combat]") {
    BattleConfig Config;
    Config.RoundTimeSec = 1.0;
    Battle Fight(Config);
    CHECK_FALSE(Fight.getResult().has_value());
    run(Fight, {}, {}, 61);
    REQUIRE(Fight.getResult().has_value());
    CHECK(Fight.getResult()->TimeSec == Approx(1.0).margin(Dt));
}

TEST_CASE("Battle: same input gives the same result", "[combat]") {
    Battle First(BattleConfig{});
    Battle Second(BattleConfig{});
    for (int Tick = 0; Tick < 300; ++Tick) {
        const PlayerCommands LeftCmd{.MoveX = (Tick / 40) % 2 ? 1.0f : -1.0f, .Jump = Tick % 90 == 0};
        const PlayerCommands RightCmd{.MoveX = (Tick / 25) % 2 ? -1.0f : 1.0f};
        First.update(LeftCmd, RightCmd, Dt);
        Second.update(LeftCmd, RightCmd, Dt);
    }
    for (auto&& [Lhs, Rhs] : std::views::zip(First.getSnapshot().Fighters, Second.getSnapshot().Fighters)) {
        CHECK(Lhs.Position == Rhs.Position);
    }
}
