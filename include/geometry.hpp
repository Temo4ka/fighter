#pragma once

#include <iostream>

#include "vec2.hpp"

#define EQUALS(x, y) (fabs((x) - (y)) < 0.0001)

enum Axis {
    Xaxis,
    Yaxis
};

struct Rect {

    enum RectSide {
        Left = 1,
        Top,
        Right,
        Bot,
    };

    double x;
    double y;
    double w;
    double h;

    explicit Rect();

    explicit Rect (double x_, double y_, double w_, double h_);

    explicit Rect (Vec2 pos, Vec2 size);

    explicit Rect (const Vec2& p1, const Vec2& p2, bool Rect_from_two_points);

    Vec2 getPos() const {return Vec2 (x, y);}

    Vec2 getSize() const {return Vec2 (w, h);}

    Vec2 getCenter() const {return Vec2 (x + w / 2, y + h / 2);}

    double left() const {return x;}

    double right() const {return x + w;}

    double top() const {return y;}

    double bot() const {return y + h;}

    bool contains (const Rect& rect) const;

    bool contains (const Vec2& vec) const;

    void print() const;

    void move(const Vec2& vec);

    static bool checkRectCollision(const Rect& rect1, const Rect& rect2);
};

bool Intersect (const Rect& rect1, const Rect& rect2);

bool HaveCommonSide (const Rect& rect1, const Rect& rect2);

Vec2 GetCollisionTime(Rect fixed, Rect moving, Vec2 vel);

