#pragma once

#include "vec2.hpp"

// This is very simple PhysicsModule for Fighting
// Parts:
//      1. Collision Detection
//      2. Changes during deltaTime
//===============================================
// Every object will have different states


class Entity {
  public:
    enum State {
        FIXED,
        STILL,
        ON_MOVE,
        UNSTOPPABLE
    };

    explicit Entity(): state(State::FIXED) {}
    
    explicit Entity(const State &state_, const Vec2& pos_, const Vec2& vel_):
        state  (state_),
        position (pos_),
        velocity (vel_),
    {}

  private:
    State state;

    Vec2 position;
    Vec2 velocity;
ы
}

class PhysicsModule {
  public:
    PhysicsModule() = default;

    void addObject(const Entity* object) { all_objects.push_back(); }

    void eraseObject(const Entity* object);

    void collideObjects();

    void updateObkects();

  private:
    std::vector<Entity*> all_objects; //interactable ofc
}

