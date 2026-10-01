//===- physics/world.hpp - RAII wrapper over a Box2D world ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares physics::World, which owns a Box2D world.
///
/// Box2D types (b2WorldId, b2Vec2, ...) never leave src/physics/
/// (docs/DEVELOPMENT_PLAN.md, section 3.1): the rest of the code sees only our
/// types, so the engine can be replaced without touching the combat logic.
///
/// Bodies, joints and contact events are added in phase 1 (the physics spike).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "core/vec2.hpp"

namespace fighter::physics {

class World {
public:
    struct Config {
        Vec2 Gravity{0.0f, -9.81f};   ///< m/s^2, Y up.
        int SubSteps = 4;             ///< Box2D solver substeps per simulation step.
    };

    World() : World(Config{}) {}
    explicit World(Config C);
    ~World();

    /// The world owns Box2D resources: it can be moved but not copied.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& Other) noexcept;
    World& operator=(World&& Other) noexcept;

    void step(float Dt);

    Vec2 getGravity() const;
    int getBodyCount() const;
    bool isValid() const { return Id != 0; }

private:
    void destroy();

    std::uint32_t Id = 0;   ///< b2WorldId packed with b2StoreWorldId; 0 is null.
    int SubSteps = 4;
};

} // namespace fighter::physics
