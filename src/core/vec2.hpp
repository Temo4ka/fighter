//===- core/vec2.hpp - 2D vector math ---------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines Vec2, the 2D vector used for all world-space math, and
/// the free functions that operate on it (dot, cross, perp, rotate, lerp).
///
/// World space is measured in meters with the Y axis pointing up (see
/// docs/DEVELOPMENT_PLAN.md, section 3.1). Components are float to match
/// Box2D and SFML without conversions.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>

namespace fighter {

/// A 2D vector in world space: meters, Y up.
struct Vec2 {
    float X = 0.0f;
    float Y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float NewX, float NewY) : X(NewX), Y(NewY) {}

    constexpr Vec2 operator-() const { return {-X, -Y}; }

    constexpr Vec2& operator+=(Vec2 O) { X += O.X; Y += O.Y; return *this; }
    constexpr Vec2& operator-=(Vec2 O) { X -= O.X; Y -= O.Y; return *this; }
    constexpr Vec2& operator*=(float S) { X *= S; Y *= S; return *this; }
    constexpr Vec2& operator/=(float S) { X /= S; Y /= S; return *this; }

    constexpr bool operator==(const Vec2&) const = default;

    constexpr float getLengthSquared() const { return X * X + Y * Y; }
    float getLength() const { return std::sqrt(getLengthSquared()); }

    /// Returns a unit vector in the same direction. The zero vector maps to the
    /// zero vector instead of NaN.
    Vec2 getNormalized() const {
        const float Len = getLength();
        return Len > 0.0f ? Vec2{X / Len, Y / Len} : Vec2{};
    }
};

constexpr Vec2 operator+(Vec2 A, Vec2 B) { return A += B; }
constexpr Vec2 operator-(Vec2 A, Vec2 B) { return A -= B; }
constexpr Vec2 operator*(Vec2 V, float S) { return V *= S; }
constexpr Vec2 operator*(float S, Vec2 V) { return V *= S; }
constexpr Vec2 operator/(Vec2 V, float S) { return V /= S; }

constexpr float dot(Vec2 A, Vec2 B) { return A.X * B.X + A.Y * B.Y; }

/// Z component of the 3D cross product: positive when B is rotated
/// counter-clockwise from A.
constexpr float cross(Vec2 A, Vec2 B) { return A.X * B.Y - A.Y * B.X; }

/// Rotates the vector by +90 degrees (counter-clockwise).
constexpr Vec2 perp(Vec2 V) { return {-V.Y, V.X}; }

/// Rotates the vector counter-clockwise by \p AngleRad radians.
inline Vec2 rotate(Vec2 V, float AngleRad) {
    const float C = std::cos(AngleRad);
    const float S = std::sin(AngleRad);
    return {V.X * C - V.Y * S, V.X * S + V.Y * C};
}

/// Linear interpolation: \p T = 0 gives \p A, \p T = 1 gives \p B.
constexpr Vec2 lerp(Vec2 A, Vec2 B, float T) { return A + (B - A) * T; }

} // namespace fighter
