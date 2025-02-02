#include "geometry.hpp"
#include <vector>

class Hitbox {
  public:
    Hitbox() = default;

    explicit Hitbox(const std::vector<Rect>& rects_) : rects(rects_) {}
    
    void addRect(const Rect& rect) {rects.push_back(rect);}

    const std::vector<Rect>& getRects() const {return rects;}

    static bool checkHitboxCollision(const Hitbox hitbox1, const Hitbox hitbox2);

  private:
    std::vector<Rect> rects;
};