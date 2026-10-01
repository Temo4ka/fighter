#pragma once

#include <cmath>

namespace fighter {

// Вектор в мировых координатах: метры, ось Y направлена вверх (docs/DEVELOPMENT_PLAN.md §3.1).
// float — как в Box2D и SFML, чтобы не терять время и точность на преобразованиях.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator-() const { return {-x, -y}; }

    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    constexpr bool operator==(const Vec2&) const = default;

    constexpr float lengthSquared() const { return x * x + y * y; }
    float length() const { return std::sqrt(lengthSquared()); }

    // Для нулевого вектора возвращает нулевой вектор, а не NaN.
    Vec2 normalized() const {
        const float len = length();
        return len > 0.0f ? Vec2{x / len, y / len} : Vec2{};
    }
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return a += b; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return a -= b; }
constexpr Vec2 operator*(Vec2 v, float s) { return v *= s; }
constexpr Vec2 operator*(float s, Vec2 v) { return v *= s; }
constexpr Vec2 operator/(Vec2 v, float s) { return v /= s; }

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// Z-компонента векторного произведения: > 0, если b повёрнут от a против часовой стрелки.
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

// Поворот на +90° (против часовой стрелки).
constexpr Vec2 perp(Vec2 v) { return {-v.y, v.x}; }

inline Vec2 rotate(Vec2 v, float angleRad) {
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

} // namespace fighter
