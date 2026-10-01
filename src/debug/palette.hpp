#pragma once

#include <cstdint>

#include "debug/category.hpp"

// Единственное место, где задаются цвета отладочных категорий.
// Без зависимости от SFML: перевод в sf::Color делает рендер.
namespace fighter::debug {

struct Rgba {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

constexpr Rgba color(Cat cat, Side side = Side::None) {
    switch (cat) {
        case Cat::Hurtbox:
            return side == Side::Right ? Rgba{70, 140, 255, 255}    // синий — правый боец
                                       : Rgba{60, 210, 90, 255};    // зелёный — левый боец
        case Cat::Hitbox:     return {240, 50, 50, 255};     // красный
        case Cat::Block:      return {250, 215, 40, 255};    // жёлтый
        case Cat::Static:     return {150, 150, 150, 255};   // серый
        case Cat::Joints:     return {245, 245, 245, 255};   // белый
        case Cat::TargetPose: return {120, 220, 255, 140};   // полупрозрачный голубой
        case Cat::Motors:     return {255, 150, 30, 255};    // оранжевый
        case Cat::Forces:     return {220, 70, 230, 255};    // пурпурный
        case Cat::Velocity:   return {40, 220, 200, 255};    // бирюзовый
        case Cat::Contacts:   return {255, 40, 80, 255};     // ярко-красный
        case Cat::CoM:        return {255, 255, 255, 255};   // крест рисуется белым с чёрной обводкой
        case Cat::Count:      break;
    }
    return {};
}

// Заливка многоугольников — тот же цвет, но полупрозрачный, чтобы было видно, что под ним.
constexpr Rgba fillColor(Cat cat, Side side = Side::None) {
    Rgba c = color(cat, side);
    c.a = static_cast<std::uint8_t>(c.a / 4);
    return c;
}

} // namespace fighter::debug
