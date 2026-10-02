//===- physics/box2d_bridge.hpp - Box2D conversions -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file holds the small helpers that convert between our types and
/// Box2D types: vectors, packed ids and the body user data that names a body
/// part.
///
/// It is private to src/physics/: it includes box2d.h, and Box2D types must
/// not leak into other modules (docs/DEVELOPMENT_PLAN.md, section 3.1).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <box2d/box2d.h>

#include "core/vec2.hpp"
#include "debug/category.hpp"

namespace fighter::physics::detail {

inline b2Vec2 toBox2D(Vec2 Value) { return {Value.X, Value.Y}; }
inline Vec2 fromBox2D(b2Vec2 Value) { return {Value.x, Value.y}; }

inline b2WorldId loadWorld(uint32_t Packed) { return b2LoadWorldId(Packed); }
inline b2BodyId loadBody(uint64_t Packed) { return b2LoadBodyId(Packed); }
inline b2JointId loadJoint(uint64_t Packed) { return b2LoadJointId(Packed); }

/// Body user data stores the index of the body in World's list of body parts
/// plus one, cast to a pointer; zero (null) means "not a body part".
inline void* encodePartSlot(uint32_t Slot) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(Slot) + 1);
}

inline std::optional<uint32_t> decodePartSlot(void* UserData) {
    const auto Raw = reinterpret_cast<uintptr_t>(UserData);
    if (Raw == 0) return std::nullopt;
    return static_cast<uint32_t>(Raw - 1);
}

/// The debug color of a shape (b2SurfaceMaterial::customColor) is the only
/// per-shape value that reaches the Box2D debug draw callbacks, so it carries
/// the debug category and the fighter: 0xF1'CC'SS (marker, category, side).
inline constexpr uint32_t DebugColorMarker = 0xF1u;

constexpr uint32_t encodeDebugColor(debug::Cat Category, debug::Side Owner) {
    return DebugColorMarker << 16 | static_cast<uint32_t>(Category) << 8 | static_cast<uint32_t>(Owner);
}

/// Draws the world through debug::draw* (debug_draw.cpp).
void drawWorldDebug(b2WorldId WorldId);

} // namespace fighter::physics::detail
