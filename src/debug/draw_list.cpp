#include "debug/draw_list.hpp"

#include <algorithm>

namespace fighter::debug {

void DrawList::beginTick() {
    primitives_.clear();
    points_.clear();
    textPool_.clear();
}

void DrawList::clearAll() {
    beginTick();
    clearPanel();
    events_.clear();
}

Primitive& DrawList::push(Primitive::Kind kind, Cat cat, Side side) {
    Primitive& p = primitives_.emplace_back();
    p.kind = kind;
    p.cat = cat;
    p.side = side;
    return p;
}

void DrawList::attachText(Primitive& p, std::string_view text) {
    p.textBegin = static_cast<std::uint32_t>(textPool_.size());
    p.textLength = static_cast<std::uint32_t>(text.size());
    textPool_.append(text);
}

void DrawList::line(Cat cat, Vec2 a, Vec2 b, Side side) {
    Primitive& p = push(Primitive::Kind::Line, cat, side);
    p.a = a;
    p.b = b;
}

void DrawList::arrow(Cat cat, Vec2 from, Vec2 vec, std::string_view label, Side side) {
    Primitive& p = push(Primitive::Kind::Arrow, cat, side);
    p.a = from;
    p.b = vec;
    attachText(p, label);
}

void DrawList::circle(Cat cat, Vec2 center, float radius, Side side) {
    Primitive& p = push(Primitive::Kind::Circle, cat, side);
    p.a = center;
    p.radius = radius;
}

void DrawList::arc(Cat cat, Vec2 center, float radius, float angle0, float angle1, Side side) {
    Primitive& p = push(Primitive::Kind::Arc, cat, side);
    p.a = center;
    p.radius = radius;
    p.angle0 = angle0;
    p.angle1 = angle1;
}

void DrawList::poly(Cat cat, std::span<const Vec2> points, Side side) {
    Primitive& p = push(Primitive::Kind::Poly, cat, side);
    p.pointsBegin = static_cast<std::uint32_t>(points_.size());
    p.pointsCount = static_cast<std::uint32_t>(points.size());
    points_.insert(points_.end(), points.begin(), points.end());
}

void DrawList::point(Cat cat, Vec2 at, float size, Side side) {
    Primitive& p = push(Primitive::Kind::Point, cat, side);
    p.a = at;
    p.radius = size;
}

void DrawList::cross(Cat cat, Vec2 at, float size, Side side) {
    Primitive& p = push(Primitive::Kind::Cross, cat, side);
    p.a = at;
    p.radius = size;
}

void DrawList::text(Cat cat, Vec2 at, std::string_view text, Side side) {
    Primitive& p = push(Primitive::Kind::Text, cat, side);
    p.a = at;
    attachText(p, text);
}

std::span<const Vec2> DrawList::points(const Primitive& p) const {
    return std::span<const Vec2>(points_).subspan(p.pointsBegin, p.pointsCount);
}

std::string_view DrawList::text(const Primitive& p) const {
    return std::string_view(textPool_).substr(p.textBegin, p.textLength);
}

void DrawList::setPanel(std::string_view key, std::string_view value) {
    auto it = std::find_if(panel_.begin(), panel_.end(), [&](const auto& kv) { return kv.first == key; });
    if (it != panel_.end()) {
        it->second = value;
    } else {
        panel_.emplace_back(std::string(key), std::string(value));
    }
}

void DrawList::clearPanel() {
    panel_.clear();
}

void DrawList::logEvent(std::string_view message) {
    events_.emplace_back(message);
    while (events_.size() > kEventLogSize) events_.pop_front();
}

} // namespace fighter::debug
