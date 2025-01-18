#include "../include/geometry.hpp"


Rect::Rect():
    x (0),
    y (0),
    w (0),
    h (0)
    {}

Rect::Rect (double x_, double y_, double w_, double h_):
    x (x_),
    y (y_),
    w (w_),
    h (h_)
    {}

Rect::Rect (Vec2 pos, Vec2 size):
    x (pos.x),
    y (pos.y),
    w (size.x),
    h (size.y)
    {}

Rect::Rect (const Vec2& p1, const Vec2& p2, bool):
    x (p1.x < p2.x ? p1.x : p2.x),
    y (p1.y < p2.y ? p1.y : p2.y),
    w (fabs (p2.x - p1.x)),
    h (fabs (p2.y - p1.y))
    {}

bool Rect::contains (const Rect& rect) const {
    return (rect.left()  >= left()  &&
            rect.right() <= right() &&
            rect.top()   >= top()   &&
            rect.bot()   <= bot()   );
}

bool Rect::contains (const Vec2& vec) const {
    return (vec.x >= left()  &&
            vec.x <= right() &&
            vec.y >= top()   &&
            vec.y <= bot()   );
}

void Rect::print () const {
    std::cerr << " Rect((" << x << ", " << y << "), "
                       "(" << w << ", " << h << ")) ";
}


void Rect::move (const Vec2& vec) {
    x += vec.x;
    y += vec.y;
}

bool Intersect (const Rect& rect1, const Rect& rect2) {
    return (rect1.left() <= rect2.right() &&
            rect2.left() <= rect1.right() &&
            rect1.top()  <= rect2.bot()   &&
            rect2.top()  <= rect1.bot()   );
}

bool HaveCommonSide (const Rect& rect1, const Rect& rect2) {
    return ((rect1.left() == rect2.left()  && rect1.right() == rect2.right() &&
            (rect1.top()  == rect2.bot()   || rect1.bot()   == rect2.top())) ||
            (rect1.top()  == rect2.top()   && rect1.bot()   == rect2.bot()   &&
            (rect1.left() == rect2.right() || rect1.right() == rect2.left())));
}

Vec2 GetCollisionTime(Rect fixed, Rect moving, Vec2 vel) {
    double dt_x = INFINITY;
    if (vel.x != 0) dt_x = (vel.x > 0 ? (moving.right() - fixed.left() )
                                      : (moving.left()  - fixed.right()) ) / vel.x;

    double dt_y = INFINITY;
    if (vel.y != 0) dt_y = (vel.y > 0 ? (moving.bot() - fixed.top())
                                      : (moving.top() - fixed.bot()) ) / vel.y;
    return Vec2 (dt_x, dt_y);
}