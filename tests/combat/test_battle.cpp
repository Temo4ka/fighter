#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/battle.hpp"

using namespace fighter::combat;
using Catch::Approx;

namespace {
constexpr double Dt = 1.0 / 60.0;

void run(Battle& B, const PlayerCommands& L, const PlayerCommands& R, int Ticks) {
    for (int I = 0; I < Ticks; ++I) B.update(L, R, Dt);
}
} // namespace

// Scenario tests of a fight without a window: scripted input -> expected state.
// For now they cover the phase 0 kinematic placeholder; agent D extends them
// once physics arrives.

TEST_CASE("Battle: fighters start on opposite sides facing each other", "[combat]") {
    Battle B(BattleConfig{});
    const auto& S = B.getSnapshot();
    CHECK(S.Fighters[0].Position.X < S.Fighters[1].Position.X);
    CHECK(S.Fighters[0].FacingRight);
    CHECK_FALSE(S.Fighters[1].FacingRight);
    CHECK(S.Fighters[0].Hp == S.Fighters[0].MaxHp);
}

TEST_CASE("Battle: walking stops at the arena wall", "[combat]") {
    Battle B(BattleConfig{});
    const float X0 = B.getSnapshot().Fighters[0].Position.X;
    run(B, {.MoveX = -1.0f}, {}, 30);
    CHECK(B.getSnapshot().Fighters[0].Position.X < X0);

    run(B, {.MoveX = -1.0f}, {}, 600);
    CHECK(B.getSnapshot().Fighters[0].Position.X > -B.getConfig().Arena.HalfWidthM);   // stayed inside
}

TEST_CASE("Battle: jump goes up and lands on the floor", "[combat]") {
    Battle B(BattleConfig{});
    run(B, {.Jump = true}, {}, 10);
    CHECK(B.getSnapshot().Fighters[0].Position.Y > 0.1f);
    run(B, {}, {}, 120);
    CHECK(B.getSnapshot().Fighters[0].Position.Y == Approx(0.0f));
}

TEST_CASE("Battle: result is available when round time runs out", "[combat]") {
    BattleConfig Config;
    Config.RoundTimeSec = 1.0;
    Battle B(Config);
    CHECK_FALSE(B.getResult().has_value());
    run(B, {}, {}, 61);
    REQUIRE(B.getResult().has_value());
    CHECK(B.getResult()->TimeSec == Approx(1.0).margin(Dt));
}

TEST_CASE("Battle: same input gives the same result", "[combat]") {
    Battle A(BattleConfig{});
    Battle B(BattleConfig{});
    for (int I = 0; I < 300; ++I) {
        const PlayerCommands L{.MoveX = (I / 40) % 2 ? 1.0f : -1.0f, .Jump = I % 90 == 0};
        const PlayerCommands R{.MoveX = (I / 25) % 2 ? -1.0f : 1.0f};
        A.update(L, R, Dt);
        B.update(L, R, Dt);
    }
    for (std::size_t F = 0; F < 2; ++F) {
        CHECK(A.getSnapshot().Fighters[F].Position == B.getSnapshot().Fighters[F].Position);
    }
}
