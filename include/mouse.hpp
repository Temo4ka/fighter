#pragma once

#include "vec2.hpp"

enum class MouseButton {
    Left,
    Right,
    Unknown
};

struct MouseContext {
    Vec2 position;
    MouseButton button;

    MouseContext(Vec2 pos, MouseButton btn):
        position (pos),
        button (btn) {}
};
