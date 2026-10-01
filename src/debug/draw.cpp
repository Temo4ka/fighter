#include "debug/draw.hpp"

#if FIGHTER_DEBUG

#include "debug/draw_list.hpp"

namespace fighter::debug {
namespace {

Side& currentSide() {
    static Side side = Side::None;
    return side;
}

} // namespace

DrawList& drawList() {
    static DrawList list;
    return list;
}

void beginTick() { drawList().beginTick(); }

void line(Cat cat, Vec2 a, Vec2 b) { drawList().line(cat, a, b, currentSide()); }

void arrow(Cat cat, Vec2 from, Vec2 vec, std::string_view label) {
    drawList().arrow(cat, from, vec, label, currentSide());
}

void circle(Cat cat, Vec2 center, float radius) { drawList().circle(cat, center, radius, currentSide()); }

void arc(Cat cat, Vec2 center, float radius, float angle0, float angle1) {
    drawList().arc(cat, center, radius, angle0, angle1, currentSide());
}

void poly(Cat cat, std::span<const Vec2> points) { drawList().poly(cat, points, currentSide()); }
void point(Cat cat, Vec2 at, float size) { drawList().point(cat, at, size, currentSide()); }
void cross(Cat cat, Vec2 at, float size) { drawList().cross(cat, at, size, currentSide()); }
void text(Cat cat, Vec2 at, std::string_view text) { drawList().text(cat, at, text, currentSide()); }

void panel(std::string_view key, std::string_view value) { drawList().setPanel(key, value); }
void event(std::string_view message) { drawList().logEvent(message); }

ScopedSide::ScopedSide(Side side) : previous_(currentSide()) { currentSide() = side; }
ScopedSide::~ScopedSide() { currentSide() = previous_; }

} // namespace fighter::debug

#endif
