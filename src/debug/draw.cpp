#include "debug/draw.hpp"

#if FIGHTER_DEBUG

#include "debug/draw_list.hpp"

namespace fighter::debug {
namespace {

Side& getCurrentSide() {
    static Side Current = Side::None;
    return Current;
}

} // namespace

DrawList& getDrawList() {
    static DrawList List;
    return List;
}

void beginTick() { getDrawList().beginTick(); }

void drawLine(Cat C, Vec2 A, Vec2 B) { getDrawList().addLine(C, A, B, getCurrentSide()); }

void drawArrow(Cat C, Vec2 From, Vec2 Vec, std::string_view Label) {
    getDrawList().addArrow(C, From, Vec, Label, getCurrentSide());
}

void drawCircle(Cat C, Vec2 Center, float Radius) {
    getDrawList().addCircle(C, Center, Radius, getCurrentSide());
}

void drawArc(Cat C, Vec2 Center, float Radius, float Angle0, float Angle1) {
    getDrawList().addArc(C, Center, Radius, Angle0, Angle1, getCurrentSide());
}

void drawPoly(Cat C, std::span<const Vec2> Vertices) {
    getDrawList().addPoly(C, Vertices, getCurrentSide());
}

void drawPoint(Cat C, Vec2 At, float Size) { getDrawList().addPoint(C, At, Size, getCurrentSide()); }
void drawCross(Cat C, Vec2 At, float Size) { getDrawList().addCross(C, At, Size, getCurrentSide()); }
void drawText(Cat C, Vec2 At, std::string_view Text) { getDrawList().addText(C, At, Text, getCurrentSide()); }

void setPanel(std::string_view Key, std::string_view Value) { getDrawList().setPanel(Key, Value); }
void logEvent(std::string_view Message) { getDrawList().logEvent(Message); }

ScopedSide::ScopedSide(Side Owner) : Previous(getCurrentSide()) { getCurrentSide() = Owner; }
ScopedSide::~ScopedSide() { getCurrentSide() = Previous; }

} // namespace fighter::debug

#endif
