#pragma once

#include <cstdint>

#include "core/vec2.hpp"

// Обёртка над миром Box2D. Типы Box2D (b2WorldId, b2Vec2, ...) не выходят за пределы
// src/physics/ (docs/DEVELOPMENT_PLAN.md §3.1): остальной код видит только наши типы,
// поэтому движок можно заменить, не трогая логику боя.
//
// Тела, шарниры и события контактов добавляются в фазе 1 (физический спайк).
namespace fighter::physics {

class World {
public:
    struct Config {
        Vec2 gravity{0.0f, -9.81f};   // м/с², Y вверх
        int subSteps = 4;             // подшаги решателя Box2D на один шаг симуляции
    };

    World() : World(Config{}) {}
    explicit World(Config config);
    ~World();

    // Мир владеет ресурсами Box2D: копировать нельзя, перемещать можно.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    void step(float dt);

    Vec2 gravity() const;
    int bodyCount() const;
    bool valid() const { return id_ != 0; }

private:
    void destroy();

    std::uint32_t id_ = 0;   // b2WorldId, упакованный через b2StoreWorldId
    int subSteps_ = 4;
};

} // namespace fighter::physics
