//===- physics/body.hpp - Rigid bodies and their shapes ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares physics::Body, a non-owning handle to a rigid body of a
/// physics::World, and the definitions used to create bodies and shapes.
///
/// The world owns every body: a handle stays valid until the world is
/// destroyed. Handles are cheap to copy and compare.
///
/// \code
///   physics::Body Thigh = PhysWorld.createBody({.Position = {0.0f, 0.7f}});
///   PhysWorld.addShape(Thigh, {.Kind = physics::ShapeKind::Capsule,
///                              .Begin = {0.0f, 0.15f}, .End = {0.0f, -0.15f},
///                              .Radius = 0.07f});
///   Thigh.setMass(7.5f);
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include "core/vec2.hpp"
#include "physics/events.hpp"

namespace fighter::physics {

enum class BodyType : uint8_t { Static, Dynamic };

struct BodyDef {
    BodyType Type = BodyType::Dynamic;
    Vec2 Position;                ///< World position of the body origin, m.
    float Angle = 0.0f;           ///< Radians, counter-clockwise.
    float LinearDamping = 0.0f;   ///< 1/s.
    float AngularDamping = 0.0f;  ///< 1/s.
    /// Set for the body parts of fighters: hit events and debug colors use it.
    std::optional<PartRef> Part;
};

enum class ShapeKind : uint8_t { Circle, Capsule, Box };

/// One collision shape in the local frame of its body.
struct ShapeDef {
    ShapeKind Kind = ShapeKind::Box;
    Vec2 Center;               ///< Circle and box: local center, m.
    Vec2 Begin;                ///< Capsule: local center of the first cap, m.
    Vec2 End;                  ///< Capsule: local center of the second cap, m.
    Vec2 HalfExtents;          ///< Box: half width (local X) and half height (local Y), m.
    float Radius = 0.0f;       ///< Circle and capsule radius; rounding of a box, m.
    float Density = 1.0f;      ///< kg/m^2. Body::setMass() rescales it.
    float Friction = 0.6f;
    float Restitution = 0.0f;
    /// Shapes with the same negative group never collide with each other
    /// (all parts of one fighter); zero means no group.
    int CollisionGroup = 0;
    bool EnableHitEvents = false;
};

/// Non-owning handle to a body of a World.
class Body {
public:
    Body() = default;

    bool isValid() const;

    /// \name State
    /// @{
    Vec2 getPosition() const;            ///< Body origin, m.
    float getAngle() const;              ///< Radians.
    Vec2 getWorldCenterOfMass() const;
    Vec2 getLinearVelocity() const;      ///< Of the center of mass, m/s.
    float getAngularVelocity() const;    ///< rad/s.
    float getMass() const;               ///< kg.
    Vec2 getWorldPoint(Vec2 LocalPoint) const;
    Vec2 getLocalPoint(Vec2 WorldPoint) const;
    /// @}

    /// Scales the density of every shape so that the body weighs \p Kg.
    void setMass(float Kg);
    void setLinearVelocity(Vec2 Velocity);

    /// \name Forces (accumulated until the next World::step)
    /// @{
    void applyForce(Vec2 Force, Vec2 WorldPoint);
    void applyForceToCenter(Vec2 Force);
    void applyTorque(float Torque);
    void applyLinearImpulseToCenter(Vec2 Impulse);
    /// @}

    bool operator==(const Body&) const = default;

private:
    friend class World;
    friend class RevoluteJoint;

    explicit Body(uint64_t Packed) : Id(Packed) {}

    uint64_t Id = 0;   ///< b2BodyId packed with b2StoreBodyId; 0 is null.
};

} // namespace fighter::physics
