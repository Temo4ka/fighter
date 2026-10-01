#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/vec2.hpp"

// Общий словарь частей тела. Лежит в core, потому что его используют модули,
// которые не должны зависеть друг от друга: stats (масса и броня частей),
// rig (физическое тело), combat (куда попал удар), render (что рисовать).
//
// Состав может поменяться по итогам физического спайка (фаза 1) — только через ревью.
namespace fighter {

enum class BodyPart : std::uint8_t {
    Head,
    Torso,
    Pelvis,
    UpperArmL, ForearmL,
    UpperArmR, ForearmR,
    ThighL, ShinL, FootL,
    ThighR, ShinR, FootR,
    Count
};

inline constexpr std::size_t kBodyPartCount = static_cast<std::size_t>(BodyPart::Count);

template <class T>
using PerBodyPart = std::array<T, kBodyPartCount>;

constexpr std::string_view bodyPartName(BodyPart part) {
    constexpr std::array<std::string_view, kBodyPartCount> names = {
        "Head", "Torso", "Pelvis",
        "UpperArmL", "ForearmL", "UpperArmR", "ForearmR",
        "ThighL", "ShinL", "FootL", "ThighR", "ShinR", "FootR",
    };
    const auto i = static_cast<std::size_t>(part);
    return i < names.size() ? names[i] : "?";
}

// Положение части тела в мире. Это всё, что рендер знает о физическом теле.
struct PartTransform {
    BodyPart part = BodyPart::Torso;
    Vec2 position;        // центр части, м
    float angle = 0.0f;   // рад, против часовой стрелки
};

} // namespace fighter
