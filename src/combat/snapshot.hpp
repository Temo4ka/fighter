#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/body.hpp"
#include "core/vec2.hpp"

// Снимок состояния боя для рендера (docs/DEVELOPMENT_PLAN.md §3.4).
// Рендер читает только снимки и ничего не знает о физике.
namespace fighter::combat {

struct FighterView {
    Vec2 position;                    // опорная точка: середина между стопами, м
    Vec2 size{0.5f, 1.8f};            // габарит; в фазе 0 боец рисуется прямоугольником
    bool facingRight = true;
    float hp = 0.0f;
    float maxHp = 0.0f;
    std::vector<PartTransform> parts; // части тела; пусто, пока нет rig (фаза 1)
};

struct ArenaView {
    float halfWidthM = 5.0f;          // стены в x = ±halfWidthM, пол в y = 0
};

struct RenderSnapshot {
    std::uint64_t tick = 0;
    double timeLeftSec = 0.0;
    ArenaView arena;
    std::array<FighterView, 2> fighters;   // [0] — левый, [1] — правый
};

// Промежуточное состояние для плавной отрисовки между шагами физики.
// alpha = 0 → prev, alpha = 1 → curr. Дискретные поля (hp, facing, tick) берутся из curr.
RenderSnapshot interpolate(const RenderSnapshot& prev, const RenderSnapshot& curr, float alpha);

} // namespace fighter::combat
