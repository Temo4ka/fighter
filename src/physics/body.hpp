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

/// Static bodies never move. Kinematic bodies move only as the code tells
/// them (moveTo()): they push dynamic bodies but feel no forces and do not
/// collide with static or other kinematic bodies. Dynamic bodies are fully
/// simulated.
enum class BodyType : uint8_t { Static, Kinematic, Dynamic };

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
    float Angle = 0.0f;        ///< Box: turn about its center in the body frame, rad.
    float Radius = 0.0f;       ///< Circle and capsule radius; rounding of a box, m.
    float Density = 1.0f;      ///< kg/m^2. Body::setMass() rescales it.
    float Friction = 0.6f;
    float Restitution = 0.0f;
    /// Shapes with the same negative group never collide with each other
    /// (all parts of one fighter); zero means no group.
    int CollisionGroup = 0;
    /// Filter bits: two shapes collide only if the category of each one is
    /// in the mask of the other.
    uint64_t CollisionCategory = 1;
    uint64_t CollisionMask = ~uint64_t{0};
    bool EnableHitEvents = false;
};

/// Non-owning handle to a body of a World.
class Body {
public:
    Body() = default;

    bool isValid() const;
    BodyType getType() const;

    /// \name State
    /// @{
    Vec2 getPosition() const;            ///< Body origin, m.
    float getAngle() const;              ///< Radians.
    Vec2 getWorldCenterOfMass() const;
    Vec2 getLinearVelocity() const;      ///< Of the center of mass, m/s.
    float getAngularVelocity() const;    ///< rad/s.
    float getMass() const;               ///< kg; 0 for static and kinematic bodies.
    /// About the center of mass, kg*m^2; 0 for static and kinematic bodies.
    float getRotationalInertia() const;
    float getGravityScale() const;
    Vec2 getWorldPoint(Vec2 LocalPoint) const;
    Vec2 getLocalPoint(Vec2 WorldPoint) const;
    /// @}

    /// Scales the density of every shape so that the body weighs \p Kg. Only
    /// a dynamic body has a mass; the densities stay when the type changes.
    void setMass(float Kg);
    void setLinearVelocity(Vec2 Velocity);
    void setAngularVelocity(float Velocity);
    /// Multiplies the world's gravity for this body: 0 floats, 1 falls.
    void setGravityScale(float Scale);
    /// Teleports the body. Meant for setting up a scene, not for motion:
    /// nothing is pushed out of the way.
    void setTransform(Vec2 Position, float Angle);
    /// Sets the velocity of a kinematic body so that it reaches \p Position
    /// and \p Angle (radians, along the shortest turn) after \p Dt. Unlike
    /// a teleport, the motion pushes dynamic bodies on its way. Box2D
    /// integrates rotation approximately, so a large turn is slightly off
    /// (about 1% of 0.2 rad); the next call corrects it, errors do not add up.
    void moveTo(Vec2 Position, float Angle, float Dt);
    /// Sets ShapeDef::CollisionMask of every shape of the body.
    void setCollisionMask(uint64_t Mask);

    bool operator==(const Body&) const = default;

private:
    friend class World;
    friend class RevoluteJoint;
    friend class SpringJoint;

    explicit Body(uint64_t Packed) : Id(Packed) {}

    uint64_t Id = 0;   ///< b2BodyId packed with b2StoreBodyId; 0 is null.
};

} // namespace fighter::physics
