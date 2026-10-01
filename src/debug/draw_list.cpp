#include "debug/draw_list.hpp"

#include <algorithm>

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

Primitive& DrawList::addPrimitive(PrimitiveKind Kind, Cat C, Side Owner) {
    Primitive& P = Primitives.emplace_back();
    P.Kind = Kind;
    P.Category = C;
    P.Owner = Owner;
    return P;
}

void DrawList::attachText(Primitive& P, std::string_view Text) {
    P.TextBegin = static_cast<std::uint32_t>(TextPool.size());
    P.TextLength = static_cast<std::uint32_t>(Text.size());
    TextPool.append(Text);
}

void DrawList::addLine(Cat C, Vec2 A, Vec2 B, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Line, C, Owner);
    P.A = A;
    P.B = B;
}

void DrawList::addArrow(Cat C, Vec2 From, Vec2 Vec, std::string_view Label, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Arrow, C, Owner);
    P.A = From;
    P.B = Vec;
    attachText(P, Label);
}

void DrawList::addCircle(Cat C, Vec2 Center, float Radius, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Circle, C, Owner);
    P.A = Center;
    P.Radius = Radius;
}

void DrawList::addArc(Cat C, Vec2 Center, float Radius, float Angle0, float Angle1, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Arc, C, Owner);
    P.A = Center;
    P.Radius = Radius;
    P.Angle0 = Angle0;
    P.Angle1 = Angle1;
}

void DrawList::addPoly(Cat C, std::span<const Vec2> Vertices, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Poly, C, Owner);
    P.PointsBegin = static_cast<std::uint32_t>(Points.size());
    P.PointsCount = static_cast<std::uint32_t>(Vertices.size());
    Points.insert(Points.end(), Vertices.begin(), Vertices.end());
}

void DrawList::addPoint(Cat C, Vec2 At, float Size, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Point, C, Owner);
    P.A = At;
    P.Radius = Size;
}

void DrawList::addCross(Cat C, Vec2 At, float Size, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Cross, C, Owner);
    P.A = At;
    P.Radius = Size;
}

void DrawList::addText(Cat C, Vec2 At, std::string_view Text, Side Owner) {
    Primitive& P = addPrimitive(PrimitiveKind::Text, C, Owner);
    P.A = At;
    attachText(P, Text);
}

std::span<const Vec2> DrawList::getPoints(const Primitive& P) const {
    return std::span<const Vec2>(Points).subspan(P.PointsBegin, P.PointsCount);
}

std::string_view DrawList::getText(const Primitive& P) const {
    return std::string_view(TextPool).substr(P.TextBegin, P.TextLength);
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
