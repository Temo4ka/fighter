#include "../include/hitbox.hpp"

static bool checkHitboxCollision(const Hitbox hitbox1, const Hitbox hitbox2) {
    for (const Rect rect1: hitbox1.getRects()) {
        for (const Rect rect2: hitbox2.getRects()) {
            if (Rect::checkRectCollision(rect1, rect2)) return true;
        }
    }
    return false;
}