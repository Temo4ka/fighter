#pragma once

// This is very simple PhysicsModule for Fighting
// Parts:
//      1. Collision Detection
//      2. Changes during deltaTime
//===============================================
// Every object will have different states

#include <vector>

#include "vec2.hpp"
#include "event.hpp"
#include "hitbox.hpp"

class Entity {
  public:
    enum State {
        FIXED,
        STILL,
        ON_MOVE,
        IN_AIR,
        UNSTOPPABLE
    };

    explicit Entity(): state(State::FIXED) {}
    
    explicit Entity(const State &state_, const Vec2& pos_, const Vec2& vel_, const double &mass_):
        state  (state_),
        position (pos_),
        velocity (vel_),
        mass    (mass_)
    {}

    void setHitbox(const Hitbox& newHitbox) { hitbox = newHitbox; }

    Hitbox getHitbox() { return hitbox; }

    State getState() { return state; }

    Vec2 position;
    Vec2 velocity;
    double mass;

  private:
    State state;

    Hitbox hitbox;
};

class PhysicsModule {
  public:
    PhysicsModule() = default;

    void addObject(Entity* object);

    void eraseObject(Entity* object);

    void collideObjects();

    void updateObjects(Time_t dt);

  private:
    std::vector<Entity*> all_objects; //interactable ofc
};

