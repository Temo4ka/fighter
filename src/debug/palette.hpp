//===- debug/palette.hpp - Debug category colors ----------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is the single place where the colors of debug categories are
/// defined. It does not depend on SFML: the renderer converts Rgba to
/// sf::Color.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "debug/category.hpp"

namespace fighter::debug {

struct Rgba {
    std::uint8_t R = 255, G = 255, B = 255, A = 255;
};

constexpr Rgba getColor(Cat C, Side Owner = Side::None) {
    switch (C) {
        case Cat::Hurtbox:
            return Owner == Side::Right ? Rgba{70, 140, 255, 255}    // blue: right fighter
                                        : Rgba{60, 210, 90, 255};    // green: left fighter
        case Cat::Hitbox:     return {240, 50, 50, 255};     // red
        case Cat::Block:      return {250, 215, 40, 255};    // yellow
        case Cat::Static:     return {150, 150, 150, 255};   // gray
        case Cat::Joints:     return {245, 245, 245, 255};   // white
        case Cat::TargetPose: return {120, 220, 255, 140};   // translucent light blue
        case Cat::Motors:     return {255, 150, 30, 255};    // orange
        case Cat::Forces:     return {220, 70, 230, 255};    // magenta
        case Cat::Velocity:   return {40, 220, 200, 255};    // teal
        case Cat::Contacts:   return {255, 40, 80, 255};     // bright red
        case Cat::CoM:        return {255, 255, 255, 255};   // white cross with a dark ring
        case Cat::Count:      break;
    }
    return {};
}

/// Polygon fill: the category color made translucent so that whatever is
/// underneath stays visible.
constexpr Rgba getFillColor(Cat C, Side Owner = Side::None) {
    Rgba Color = getColor(C, Owner);
    Color.A = static_cast<std::uint8_t>(Color.A / 4);
    return Color;
}

} // namespace fighter::debug
