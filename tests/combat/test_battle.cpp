#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/battle.hpp"

using namespace fighter::combat;
using Catch::Approx;

namespace {
constexpr double kDt = 1.0 / 60.0;

void run(Battle& b, const PlayerCommands& l, const PlayerCommands& r, int ticks) {
    for (int i = 0; i < ticks; ++i) b.update(l, r, kDt);
}
} // namespace

// Сценарные тесты боя без окна: скриптованный ввод → ожидаемое состояние.
// Пока проверяют кинематическую заглушку фазы 0; с появлением физики их дополнит агент D.

TEST_CASE("Battle: бойцы стартуют по разные стороны и смотрят друг на друга", "[combat]") {
    Battle b(BattleConfig{});
    const auto& s = b.snapshot();
    CHECK(s.fighters[0].position.x < s.fighters[1].position.x);
    CHECK(s.fighters[0].facingRight);
    CHECK_FALSE(s.fighters[1].facingRight);
    CHECK(s.fighters[0].hp == s.fighters[0].maxHp);
}

TEST_CASE("Battle: ходьба и стена арены", "[combat]") {
    Battle b(BattleConfig{});
    const float x0 = b.snapshot().fighters[0].position.x;
    run(b, {.moveX = -1.0f}, {}, 30);
    CHECK(b.snapshot().fighters[0].position.x < x0);

    run(b, {.moveX = -1.0f}, {}, 600);
    CHECK(b.snapshot().fighters[0].position.x > -b.config().arena.halfWidthM);   // не вышел за стену
}

TEST_CASE("Battle: прыжок поднимает и возвращает на пол", "[combat]") {
    Battle b(BattleConfig{});
    run(b, {.jump = true}, {}, 10);
    CHECK(b.snapshot().fighters[0].position.y > 0.1f);
    run(b, {}, {}, 120);
    CHECK(b.snapshot().fighters[0].position.y == Approx(0.0f));
}

TEST_CASE("Battle: по истечении времени раунда есть результат", "[combat]") {
    BattleConfig config;
    config.roundTimeSec = 1.0;
    Battle b(config);
    CHECK_FALSE(b.result().has_value());
    run(b, {}, {}, 61);
    REQUIRE(b.result().has_value());
    CHECK(b.result()->timeSec == Approx(1.0).margin(kDt));
}

TEST_CASE("Battle: одинаковый ввод даёт одинаковый результат", "[combat]") {
    Battle a(BattleConfig{});
    Battle b(BattleConfig{});
    for (int i = 0; i < 300; ++i) {
        const PlayerCommands l{.moveX = (i / 40) % 2 ? 1.0f : -1.0f, .jump = i % 90 == 0};
        const PlayerCommands r{.moveX = (i / 25) % 2 ? -1.0f : 1.0f};
        a.update(l, r, kDt);
        b.update(l, r, kDt);
    }
    for (std::size_t f = 0; f < 2; ++f) {
        CHECK(a.snapshot().fighters[f].position == b.snapshot().fighters[f].position);
    }
}
