#include "physics/box2d_bridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <box2d/box2d.h>

#include "debug/draw.hpp"

// Box2D debug draw (b2DebugDraw callbacks) routed into debug::draw*.
//
// b2World_Draw is called once per kind of data, and each pass maps its
// primitives to one debug category: shapes -> Hurtbox/Static (taken from the
// shape's color tag, see encodeDebugColor), joints -> Joints, contacts ->
// Contacts.

namespace fighter::physics::detail {
namespace {

/// Box2D gives point sizes in pixels; this turns them into meters.
constexpr float MetersPerPixel = 0.006f;
/// Segments per half circle of a capsule outline.
constexpr int CapSegments = 8;

struct DrawPass {
    debug::Cat Category = debug::Cat::Static;
};

void drawPass(b2WorldId WorldId, debug::Cat Category, b2DebugDraw Settings);
b2DebugDraw makeCallbacks();

} // namespace

void drawWorldDebug(b2WorldId WorldId) {
    // The category of a shape comes from its color tag, not from the pass.
    b2DebugDraw Shapes = makeCallbacks();
    Shapes.drawShapes = true;
    drawPass(WorldId, debug::Cat::Static, Shapes);

    b2DebugDraw Joints = makeCallbacks();
    Joints.drawJoints = true;
    drawPass(WorldId, debug::Cat::Joints, Joints);

    b2DebugDraw Contacts = makeCallbacks();
    Contacts.drawContacts = true;
    Contacts.drawContactNormals = true;
    drawPass(WorldId, debug::Cat::Contacts, Contacts);
}

namespace {

/// Category and side of a shape from its color tag; untagged shapes are
/// treated as arena geometry.
struct ShapeTag {
    debug::Cat Category = debug::Cat::Static;
    debug::Side Owner = debug::Side::None;
};

ShapeTag decodeTag(b2HexColor Color) {
    const auto Raw = static_cast<uint32_t>(Color);
    if (Raw >> 16 != DebugColorMarker) return {};
    return {static_cast<debug::Cat>(Raw >> 8 & 0xFFu), static_cast<debug::Side>(Raw & 0xFFu)};
}

debug::Cat getCategory(void* Context) { return static_cast<const DrawPass*>(Context)->Category; }

void drawPolygon(const b2Vec2* Vertices, int Count, b2HexColor, void* Context) {
    std::vector<Vec2> Points;
    Points.reserve(static_cast<size_t>(Count));
    for (const auto& Vertex : std::span(Vertices, static_cast<size_t>(Count))) Points.push_back(fromBox2D(Vertex));
    debug::drawPoly(getCategory(Context), Points);
}

void drawSolidPolygon(b2Transform Transform, const b2Vec2* Vertices, int Count, float, b2HexColor Color, void*) {
    const ShapeTag Tag = decodeTag(Color);
    debug::ScopedSide Owner(Tag.Owner);
    std::vector<Vec2> Points;
    Points.reserve(static_cast<size_t>(Count));
    for (const auto& Vertex : std::span(Vertices, static_cast<size_t>(Count))) {
        Points.push_back(fromBox2D(b2TransformPoint(Transform, Vertex)));
    }
    debug::drawPoly(Tag.Category, Points);
}

void drawCircle(b2Vec2 Center, float Radius, b2HexColor, void* Context) {
    debug::drawCircle(getCategory(Context), fromBox2D(Center), Radius);
}

void drawSolidCircle(b2Transform Transform, float Radius, b2HexColor Color, void*) {
    const ShapeTag Tag = decodeTag(Color);
    debug::ScopedSide Owner(Tag.Owner);
    const Vec2 Center = fromBox2D(Transform.p);
    debug::drawCircle(Tag.Category, Center, Radius);
    // A radius line shows how the circle is rotated.
    debug::drawLine(Tag.Category, Center, Center + fromBox2D(b2RotateVector(Transform.q, {Radius, 0.0f})));
}

void drawSolidCapsule(b2Vec2 Begin, b2Vec2 End, float Radius, b2HexColor Color, void*) {
    const ShapeTag Tag = decodeTag(Color);
    debug::ScopedSide Owner(Tag.Owner);

    constexpr float Pi = std::numbers::pi_v<float>;
    const Vec2 From = fromBox2D(Begin);
    const Vec2 To = fromBox2D(End);
    const Vec2 Axis = (To - From).getNormalized();
    const Vec2 Side = perp(Axis) * Radius;

    // Outline: half circle around End, then half circle around Begin.
    std::array<Vec2, 2 * (CapSegments + 1)> Outline;
    for (int Index = 0; Index <= CapSegments; ++Index) {
        const float Angle = Pi * static_cast<float>(Index) / static_cast<float>(CapSegments);
        Outline[static_cast<size_t>(Index)] = To + rotate(-Side, Angle);
        Outline[static_cast<size_t>(Index + CapSegments + 1)] = From + rotate(Side, Angle);
    }
    debug::drawPoly(Tag.Category, Outline);
}

void drawSegment(b2Vec2 Begin, b2Vec2 End, b2HexColor, void* Context) {
    debug::drawLine(getCategory(Context), fromBox2D(Begin), fromBox2D(End));
}

void drawTransform(b2Transform, void*) {}

void drawPoint(b2Vec2 At, float SizePx, b2HexColor, void* Context) {
    debug::drawPoint(getCategory(Context), fromBox2D(At), SizePx * MetersPerPixel);
}

void drawString(b2Vec2 At, const char* Text, b2HexColor, void* Context) {
    debug::drawText(getCategory(Context), fromBox2D(At), Text);
}

b2DebugDraw makeCallbacks() {
    b2DebugDraw Draw = b2DefaultDebugDraw();
    Draw.DrawPolygonFcn = drawPolygon;
    Draw.DrawSolidPolygonFcn = drawSolidPolygon;
    Draw.DrawCircleFcn = drawCircle;
    Draw.DrawSolidCircleFcn = drawSolidCircle;
    Draw.DrawSolidCapsuleFcn = drawSolidCapsule;
    Draw.DrawSegmentFcn = drawSegment;
    Draw.DrawTransformFcn = drawTransform;
    Draw.DrawPointFcn = drawPoint;
    Draw.DrawStringFcn = drawString;
    Draw.drawShapes = false;
    Draw.drawJoints = false;
    return Draw;
}

void drawPass(b2WorldId WorldId, debug::Cat Category, b2DebugDraw Settings) {
    DrawPass Pass{Category};
    Settings.context = &Pass;
    b2World_Draw(WorldId, &Settings);
}

} // namespace

} // namespace fighter::physics::detail
