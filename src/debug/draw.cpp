#include "debug/draw.hpp"
#include <cstdio> // TMPDBG
#include <cstdlib> // TMPDBG

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

void drawLine(Cat Category, Vec2 From, Vec2 To) { getDrawList().addLine(Category, From, To, getCurrentSide()); }

void drawArrow(Cat Category, Vec2 From, Vec2 Vec, std::string_view Label) {
    getDrawList().addArrow(Category, From, Vec, Label, getCurrentSide());
}

void drawCircle(Cat Category, Vec2 Center, float Radius) {
    getDrawList().addCircle(Category, Center, Radius, getCurrentSide());
}

void drawArc(Cat Category, Vec2 Center, float Radius, float Angle0, float Angle1) {
    getDrawList().addArc(Category, Center, Radius, Angle0, Angle1, getCurrentSide());
}

void drawPoly(Cat Category, std::span<const Vec2> Vertices) {
    getDrawList().addPoly(Category, Vertices, getCurrentSide());
}

void drawPoint(Cat Category, Vec2 At, float Size) { getDrawList().addPoint(Category, At, Size, getCurrentSide()); }
void drawCross(Cat Category, Vec2 At, float Size) { getDrawList().addCross(Category, At, Size, getCurrentSide()); }
void drawText(Cat Category, Vec2 At, std::string_view Text) { getDrawList().addText(Category, At, Text, getCurrentSide()); }

void setPanel(std::string_view Key, std::string_view Value) { getDrawList().setPanel(Key, Value); }
void logEvent(std::string_view Message) { if (std::getenv("FIGHTER_LOG")) std::fprintf(stderr, "%.*s\n", int(Message.size()), Message.data()); getDrawList().logEvent(Message); } // TMPDBG

ScopedSide::ScopedSide(Side Owner) : Previous(getCurrentSide()) { getCurrentSide() = Owner; }
ScopedSide::~ScopedSide() { getCurrentSide() = Previous; }

} // namespace fighter::debug

#endif
