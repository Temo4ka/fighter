#pragma once

#include <cstdint>

#include "core/body.hpp"
#include "core/vec2.hpp"

// Контракт physics → combat (docs/DEVELOPMENT_PLAN.md §4).
// Меняется только через ревью.
namespace fighter::physics {

// Ссылка на часть тела конкретного бойца.
struct PartRef {
    std::uint8_t fighter = 0;   // 0 — левый, 1 — правый
    BodyPart part = BodyPart::Torso;
};

// Удар: одна часть тела попала в другую.
// Урон считает combat — из импульса и брони части тела жертвы.
struct HitEvent {
    PartRef attacker;
    PartRef victim;
    Vec2 point;                 // точка контакта, м
    float approachSpeed = 0.0f; // скорость сближения в момент удара, м/с
    float impulse = 0.0f;       // импульс контакта, Н·с
};

} // namespace fighter::physics
