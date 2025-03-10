#pragma once

#include "vec2.hpp"

enum class MouseButton {
    Left = 0,
    Right,
    Unknown
};

MouseButton getMouseButton(int code) {
    switch (code) {
        case (int) MouseButton::Left: 
            return MouseButton::Left;
        case (int) MouseButton::Right:
            return MouseButton::Right;

        default:
            return MouseButton::Unknown;
    }
}

struct MouseContext {
    Vec2 position;
    MouseButton button;

    MouseContext(Vec2 &pos, MouseButton btn):
        position (pos),
        button (btn) {}
};
