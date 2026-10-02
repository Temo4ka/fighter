#include "debug/draw_list.hpp"

#include <algorithm>
#include <cstdint>

namespace fighter::debug {

void DrawList::beginTick() {
    Primitives.clear();
    Points.clear();
    TextPool.clear();
}

void DrawList::clearAll() {
    beginTick();
    clearPanel();
    Events.clear();
}

Primitive& DrawList::addPrimitive(PrimitiveKind Kind, Cat Category, Side Owner) {
    Primitive& Prim = Primitives.emplace_back();
    Prim.Kind = Kind;
    Prim.Category = Category;
    Prim.Owner = Owner;
    return Prim;
}

void DrawList::attachText(Primitive& Prim, std::string_view Text) {
    Prim.TextBegin = static_cast<uint32_t>(TextPool.size());
    Prim.TextLength = static_cast<uint32_t>(Text.size());
    TextPool.append(Text);
}

void DrawList::addLine(Cat Category, Vec2 From, Vec2 To, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Line, Category, Owner);
    Prim.Anchor = From;
    Prim.End = To;
}

void DrawList::addArrow(Cat Category, Vec2 From, Vec2 Vec, std::string_view Label, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Arrow, Category, Owner);
    Prim.Anchor = From;
    Prim.End = From + Vec;
    attachText(Prim, Label);
}

void DrawList::addCircle(Cat Category, Vec2 Center, float Radius, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Circle, Category, Owner);
    Prim.Anchor = Center;
    Prim.Radius = Radius;
}

void DrawList::addArc(Cat Category, Vec2 Center, float Radius, float Angle0, float Angle1, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Arc, Category, Owner);
    Prim.Anchor = Center;
    Prim.Radius = Radius;
    Prim.Angle0 = Angle0;
    Prim.Angle1 = Angle1;
}

void DrawList::addPoly(Cat Category, std::span<const Vec2> Vertices, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Poly, Category, Owner);
    Prim.PointsBegin = static_cast<uint32_t>(Points.size());
    Prim.PointsCount = static_cast<uint32_t>(Vertices.size());
    Points.insert(Points.end(), Vertices.begin(), Vertices.end());
}

void DrawList::addPoint(Cat Category, Vec2 At, float Size, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Point, Category, Owner);
    Prim.Anchor = At;
    Prim.Radius = Size;
}

void DrawList::addCross(Cat Category, Vec2 At, float Size, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Cross, Category, Owner);
    Prim.Anchor = At;
    Prim.Radius = Size;
}

void DrawList::addText(Cat Category, Vec2 At, std::string_view Text, Side Owner) {
    Primitive& Prim = addPrimitive(PrimitiveKind::Text, Category, Owner);
    Prim.Anchor = At;
    attachText(Prim, Text);
}

std::span<const Vec2> DrawList::getPoints(const Primitive& Prim) const {
    return std::span<const Vec2>(Points).subspan(Prim.PointsBegin, Prim.PointsCount);
}

std::string_view DrawList::getText(const Primitive& Prim) const {
    return std::string_view(TextPool).substr(Prim.TextBegin, Prim.TextLength);
}

void DrawList::setPanel(std::string_view Key, std::string_view Value) {
    auto It = std::find_if(Panel.begin(), Panel.end(), [&](const auto& KV) { return KV.first == Key; });
    if (It != Panel.end()) {
        It->second = Value;
    } else {
        Panel.emplace_back(std::string(Key), std::string(Value));
    }
}

void DrawList::clearPanel() {
    Panel.clear();
}

void DrawList::logEvent(std::string_view Message) {
    Events.emplace_back(Message);
    while (Events.size() > EventLogSize) Events.pop_front();
}

} // namespace fighter::debug
