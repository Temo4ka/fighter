#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>

struct Vec2 {
    double x;
    double y;

    explicit Vec2 ();

    explicit Vec2 (double x_, double y_);

    Vec2 operator-() const;

    double GetLen() const;

    void Normalize();

    Vec2 operator!() const;

    void Rotate (const double angle);

    double GetAngle();
    
    // Новые методы для улучшенной физики
    Vec2 normalized() const;
    double dot(const Vec2& other) const;
    double length() const { return GetLen(); }
    double lengthSquared() const { return x*x + y*y; }
    
    // Операторы сравнения
    bool operator<(const Vec2& other) const;
    bool operator<=(const Vec2& other) const;
    bool operator>(const Vec2& other) const;
    bool operator>=(const Vec2& other) const;
};

Vec2 operator+= (Vec2& vec1, const Vec2& vec2);

Vec2 operator-= (Vec2& vec1, const Vec2& vec2);

Vec2 operator*= (Vec2& vec, const double scalar);

Vec2 operator/= (Vec2& vec, const double scalar);

Vec2 operator+ (Vec2 vec1, const Vec2& vec2);

Vec2 operator- (Vec2 vec1, const Vec2& vec2);

Vec2 operator* (Vec2 vec, const double scalar);

Vec2 operator* (const double scalar, Vec2 vec);

Vec2 operator/ (Vec2 vec, const double scalar);

double operator, (const Vec2& vec1, const Vec2& vec2);

Vec2 operator^ (const Vec2& vec1, const Vec2& vec2);

bool operator== (const Vec2& vec1, const Vec2& vec2);

double GetRandAngle ();