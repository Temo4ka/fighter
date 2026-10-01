#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fighter::debug {

// Категории отладочных примитивов (docs/DEVELOPMENT_PLAN.md §3.5).
// У каждой свой цвет (palette.hpp) и своя клавиша включения в debug-сборке.
enum class Cat : std::uint8_t {
    Hurtbox,     // части тела, по которым можно попасть
    Hitbox,      // ударные части в активной фазе удара
    Block,       // зоны блока
    Static,      // пол, стены арены
    Joints,      // шарниры и пределы углов
    TargetPose,  // «призрак» целевой позы
    Motors,      // моменты моторов шарниров
    Forces,      // силы и импульсы
    Velocity,    // скорости
    Contacts,    // точки контакта и нормали
    CoM,         // центр масс и опорная точка
    Count
};

inline constexpr std::size_t kCatCount = static_cast<std::size_t>(Cat::Count);

constexpr std::string_view catName(Cat cat) {
    constexpr std::array<std::string_view, kCatCount> names = {
        "Hurtbox", "Hitbox", "Block", "Static", "Joints", "TargetPose",
        "Motors", "Forces", "Velocity", "Contacts", "CoM",
    };
    const auto i = static_cast<std::size_t>(cat);
    return i < names.size() ? names[i] : "?";
}

// Чей это примитив. Некоторые категории (Hurtbox) красятся по-разному у бойцов.
enum class Side : std::uint8_t { None, Left, Right };

} // namespace fighter::debug
