#pragma once

#include <array>
#include <optional>
#include <string>

#include "combat/commands.hpp"
#include "combat/snapshot.hpp"
#include "core/vec2.hpp"
#include "stats/stats.hpp"

// Публичный API модуля боя (docs/DEVELOPMENT_PLAN.md §4).
// Внешний проект создаёт Battle из конфигурации, кормит его вводом
// и забирает результат. Контракты меняются только через ревью.
namespace fighter::combat {

struct FighterConfig {
    stats::Stats stats;
    stats::Loadout loadout;
    std::string rigId = "humanoid";
};

struct ArenaConfig {
    float halfWidthM = 5.0f;
    Vec2 gravity{0.0f, -9.81f};
};

struct BattleConfig {
    FighterConfig left;
    FighterConfig right;
    ArenaConfig arena;
    double roundTimeSec = 90.0;
};

enum class Winner { Left, Right, Draw };

struct FighterReport {
    float hpLeft = 0.0f;
    float damageDealt = 0.0f;
};

struct BattleResult {
    Winner winner = Winner::Draw;
    double timeSec = 0.0;
    FighterReport left;
    FighterReport right;
};

// ЗАГЛУШКА фазы 0: бойцы — кинематические прямоугольники (ходьба, прыжок, стены),
// ударов нет. Нужна, чтобы проверить ввод, цикл, рендер и debug-слой.
// В фазах 1–2 внутренности заменяются на rig + физику; интерфейс остаётся.
class Battle {
public:
    explicit Battle(const BattleConfig& config);

    void update(const PlayerCommands& left, const PlayerCommands& right, double dt);

    const RenderSnapshot& snapshot() const { return snapshot_; }
    std::optional<BattleResult> result() const { return result_; }
    const BattleConfig& config() const { return config_; }

private:
    struct FighterState {
        Vec2 position;
        Vec2 velocity;
        bool grounded = true;
        bool facingRight = true;
        float hp = 0.0f;
        stats::PhysicalProfile profile;
    };

    void updateFighter(FighterState& f, const PlayerCommands& cmd, float dt);
    void finish(Winner winner);
    void publishSnapshot();
    void drawDebug() const;

    BattleConfig config_;
    std::array<FighterState, 2> fighters_;
    double elapsedSec_ = 0.0;
    std::uint64_t tick_ = 0;
    RenderSnapshot snapshot_;
    std::optional<BattleResult> result_;
};

} // namespace fighter::combat
