#pragma once

#include "geometry.hpp"
#include <vector>

class Hitbox {
  public:

    enum Type {
        Basic,
        Attack,
        Block
    };

    Hitbox() = default;

    explicit Hitbox(const std::vector<Rect>& rects_, const Type type_) :
        rects(rects_),
        type (type_)
    {}
    
    void addRect(const Rect& rect) { rects.push_back(rect); }

    std::vector<Rect> getRects() const { return rects; }

    std::vector<Rect>& getRects() { return rects; }

    static bool checkHitboxCollision(const Hitbox& hitbox1, const Hitbox& hitbox2);

    Type getType() const { return type; }

    bool active = true;

  private:
    std::vector<Rect> rects;

    Type type = Type::Basic;
};