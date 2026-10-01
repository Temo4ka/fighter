//===- debug/category.hpp - Debug primitive categories ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the categories of debug primitives (Cat) and the side a
/// primitive belongs to (Side), see docs/DEVELOPMENT_PLAN.md, section 3.5.
///
/// Each category has its own color (debug/palette.hpp) and its own toggle key
/// in the debug build.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fighter::debug {

enum class Cat : std::uint8_t {
    Hurtbox,     ///< Body parts that can be hit.
    Hitbox,      ///< Striking parts during the active phase of an attack.
    Block,       ///< Blocking zones.
    Static,      ///< Arena floor and walls.
    Joints,      ///< Joints and their angle limits.
    TargetPose,  ///< "Ghost" of the target pose.
    Motors,      ///< Joint motor torques.
    Forces,      ///< Forces and impulses.
    Velocity,    ///< Velocities.
    Contacts,    ///< Contact points and normals.
    CoM,         ///< Center of mass and support point.
    Count
};

inline constexpr std::size_t CatCount = static_cast<std::size_t>(Cat::Count);

constexpr std::string_view getCatName(Cat C) {
    constexpr std::array<std::string_view, CatCount> Names = {
        "Hurtbox", "Hitbox", "Block", "Static", "Joints", "TargetPose",
        "Motors", "Forces", "Velocity", "Contacts", "CoM",
    };
    const auto I = static_cast<std::size_t>(C);
    return I < Names.size() ? Names[I] : "?";
}

/// Which fighter a primitive belongs to. Some categories (Hurtbox) use a
/// different color for each fighter.
enum class Side : std::uint8_t { None, Left, Right };

} // namespace fighter::debug
