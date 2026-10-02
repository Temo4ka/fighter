//===- physics/world.hpp - RAII wrapper over a Box2D world ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares physics::World, which owns a Box2D world with its
/// bodies, shapes and joints, steps it and reports hits between body parts.
///
/// Box2D types (b2WorldId, b2Vec2, ...) never leave src/physics/
/// (docs/DEVELOPMENT_PLAN.md, section 3.1): the rest of the code sees only our
/// types, so the engine can be replaced without touching the combat logic.
///
/// No user callbacks run inside step(): hits are collected after the Box2D
/// step and read with getHitEvents(), which keeps the order of work explicit
/// and the simulation deterministic.
///
/// Body parts may be kinematic (moved by code, see Body::moveTo). The solver
/// treats them as infinitely heavy, so the contact impulse of a hit that
/// involves one says nothing about the strike; such hits report the impulse
/// of the same collision between free bodies with the parts' masses instead
/// (for a kinematic part, the mass of the limb it strikes with).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/vec2.hpp"
#include "physics/body.hpp"
#include "physics/events.hpp"
#include "physics/joint.hpp"

namespace fighter::physics {

class World {
public:
    struct Config {
        Vec2 Gravity{0.0f, -9.81f};   ///< m/s^2, Y up.
        int SubSteps = 4;             ///< Box2D solver substeps per simulation step.
        /// Closing speed above which a contact between body parts of
        /// different fighters is reported as a hit, m/s.
        float HitSpeedThreshold = 1.0f;
    };

    World() : World(Config{}) {}
    explicit World(Config Settings);
    ~World();

    /// The world owns Box2D resources: it can be moved but not copied.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& Other) noexcept;
    World& operator=(World&& Other) noexcept;

    /// \name Construction
    /// Bodies, shapes and joints live as long as the world.
    /// @{
    Body createBody(const BodyDef& Def);
    void addShape(Body Target, const ShapeDef& Shape);
    RevoluteJoint createRevoluteJoint(const RevoluteJointDef& Def);
    /// @}

    /// Switches a body between kinematic and dynamic. Set the mass of a body
    /// part while it is dynamic: a kinematic body has no mass in the solver,
    /// and the world keeps the last dynamic mass for the impulse of hits.
    void setBodyType(Body Target, BodyType Type);
    /// The mass a body part hits with while it is kinematic, kg: a posed limb
    /// strikes with the limb behind it, not only with the touching part. By
    /// default it is the part's own last dynamic mass.
    void setStrikeMass(Body Target, float Kg);

    /// Advances the simulation by \p Dt and collects the hits of this step.
    void step(float Dt);

    /// Hits between body parts of different fighters during the last step,
    /// in a deterministic order.
    std::span<const HitEvent> getHitEvents() const { return Hits; }

    /// Routes Box2D debug drawing into debug::draw*: shapes go to Hurtbox
    /// (colored by fighter) or Static, joints to Joints, contact points and
    /// normals to Contacts. Does nothing in the release build.
    void drawDebug() const;

    Vec2 getGravity() const;
    int getBodyCount() const;
    int getJointCount() const;
    bool isValid() const { return Id != 0; }

private:
    /// A body that belongs to a fighter, with its state before the last step:
    /// after the step the velocities already include the hit itself.
    struct PartBody {
        Body Handle;
        PartRef Part;
        Vec2 CenterBeforeStep;
        Vec2 VelocityBeforeStep;
        float AngularVelocityBeforeStep = 0.0f;
        /// Mass of the body while it was last dynamic, kg.
        float DynamicMass = 0.0f;
        /// setStrikeMass(); 0 means DynamicMass.
        float StrikeMass = 0.0f;
    };

    void destroy();
    void recordPartVelocities();
    void collectHits();
    Vec2 getVelocityBeforeStep(const PartBody& Entry, Vec2 WorldPoint) const;
    /// Mass of a part for the impulse of a hit: its strike mass if it is
    /// kinematic now, kg.
    float getStrikeMass(const PartBody& Entry) const;

    uint32_t Id = 0;   ///< b2WorldId packed with b2StoreWorldId; 0 is null.
    int SubSteps = 4;
    std::vector<PartBody> PartBodies;
    std::vector<HitEvent> Hits;
};

} // namespace fighter::physics
