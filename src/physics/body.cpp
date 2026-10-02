#include "physics/body.hpp"

#include <array>
#include <cstddef>
#include <span>

#include <box2d/box2d.h>

#include "physics/box2d_bridge.hpp"

namespace fighter::physics {

using detail::fromBox2D;
using detail::loadBody;
using detail::toBox2D;

namespace {

/// A body part has one or two shapes; more is never needed.
constexpr int MaxShapesPerBody = 8;

} // namespace

bool Body::isValid() const { return Id != 0 && b2Body_IsValid(loadBody(Id)); }

BodyType Body::getType() const { return detail::fromBox2D(b2Body_GetType(loadBody(Id))); }

Vec2 Body::getPosition() const { return fromBox2D(b2Body_GetPosition(loadBody(Id))); }

float Body::getAngle() const { return b2Rot_GetAngle(b2Body_GetRotation(loadBody(Id))); }

Vec2 Body::getWorldCenterOfMass() const { return fromBox2D(b2Body_GetWorldCenterOfMass(loadBody(Id))); }

Vec2 Body::getLinearVelocity() const { return fromBox2D(b2Body_GetLinearVelocity(loadBody(Id))); }

float Body::getAngularVelocity() const { return b2Body_GetAngularVelocity(loadBody(Id)); }

float Body::getMass() const { return b2Body_GetMass(loadBody(Id)); }

Vec2 Body::getWorldPoint(Vec2 LocalPoint) const {
    return fromBox2D(b2Body_GetWorldPoint(loadBody(Id), toBox2D(LocalPoint)));
}

Vec2 Body::getLocalPoint(Vec2 WorldPoint) const {
    return fromBox2D(b2Body_GetLocalPoint(loadBody(Id), toBox2D(WorldPoint)));
}

void Body::setMass(float Kg) {
    const b2BodyId BodyId = loadBody(Id);
    const float Current = b2Body_GetMass(BodyId);
    if (Current <= 0.0f || Kg <= 0.0f) return;

    const float Scale = Kg / Current;
    std::array<b2ShapeId, MaxShapesPerBody> Shapes{};
    const int Count = b2Body_GetShapes(BodyId, Shapes.data(), MaxShapesPerBody);
    for (const auto& Shape : std::span(Shapes).first(static_cast<size_t>(Count))) {
        b2Shape_SetDensity(Shape, b2Shape_GetDensity(Shape) * Scale, false);
    }
    b2Body_ApplyMassFromShapes(BodyId);
}

void Body::setLinearVelocity(Vec2 Velocity) { b2Body_SetLinearVelocity(loadBody(Id), toBox2D(Velocity)); }

void Body::setAngularVelocity(float Velocity) { b2Body_SetAngularVelocity(loadBody(Id), Velocity); }

void Body::setTransform(Vec2 Position, float Angle) {
    b2Body_SetTransform(loadBody(Id), toBox2D(Position), b2MakeRot(Angle));
}

void Body::moveTo(Vec2 Position, float Angle, float Dt) {
    // Like b2Body_SetTargetTransform, but without its "too slow to wake up"
    // early return: that would leave the previous velocity in place, and a
    // body that should stop would keep drifting.
    if (Dt <= 0.0f) return;
    const b2BodyId BodyId = loadBody(Id);
    const b2Vec2 Offset = b2Sub(toBox2D(Position), b2Body_GetPosition(BodyId));
    const float Turn = b2RelativeAngle(b2MakeRot(Angle), b2Body_GetRotation(BodyId));
    b2Body_SetLinearVelocity(BodyId, b2MulSV(1.0f / Dt, Offset));
    b2Body_SetAngularVelocity(BodyId, Turn / Dt);
}

void Body::setCollisionMask(uint64_t Mask) {
    std::array<b2ShapeId, MaxShapesPerBody> Shapes{};
    const int Count = b2Body_GetShapes(loadBody(Id), Shapes.data(), MaxShapesPerBody);
    for (const auto& Shape : std::span(Shapes).first(static_cast<size_t>(Count))) {
        b2Filter Filter = b2Shape_GetFilter(Shape);
        Filter.maskBits = Mask;
        b2Shape_SetFilter(Shape, Filter);
    }
}

} // namespace fighter::physics
