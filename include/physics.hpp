#pragma once

// Enhanced PhysicsModule for Fighting Game
// Features:
//      1. Advanced Collision Detection with Elastic Collisions
//      2. Friction and Air Resistance
//      3. Impulse-based Physics
//      4. Spatial Partitioning for Performance
//      5. Enhanced Entity States and Physics Properties
//===============================================

#include <vector>
#include <unordered_map>
#include <memory>

#include "vec2.hpp"
#include "event.hpp"
#include "hitbox.hpp"

class Entity {
  public:
    enum State {
        FIXED,        // Неподвижный объект (платформы, стены)
        STILL,        // Покой
        ON_MOVE,      // В движении
        IN_AIR,       // В воздухе
        UNSTOPPABLE,  // Неостанавливаемый
        SLIDING,      // Скольжение
        ROLLING       // Качение
    };

    explicit Entity(): 
        state(State::FIXED), 
        friction(0.0), 
        airResistance(0.0),
        elasticity(0.5),
        maxVelocity(100.0),
        angularVelocity(0.0),
        rotation(0.0) {}
    
    explicit Entity(const State &state_, const Vec2& pos_, const Vec2& vel_, const double &mass_):
        state  (state_),
        position (pos_),
        velocity (vel_),
        mass    (mass_),
        friction(0.8),
        airResistance(0.02),
        elasticity(0.5),
        maxVelocity(100.0),
        angularVelocity(0.0),
        rotation(0.0) {}

    void setHitbox(const Hitbox& newHitbox) { hitbox = newHitbox; }
    Hitbox getHitbox() const { return hitbox; }
    State getState() { return state; }
    void setState(State newState) { state = newState; }
    
    // Физические свойства
    void setFriction(double f) { friction = f; }
    void setAirResistance(double ar) { airResistance = ar; }
    void setElasticity(double e) { elasticity = e; }
    void setMaxVelocity(double mv) { maxVelocity = mv; }
    
    double getFriction() const { return friction; }
    double getAirResistance() const { return airResistance; }
    double getElasticity() const { return elasticity; }
    double getMaxVelocity() const { return maxVelocity; }

    Vec2 position;
    Vec2 velocity;
    double mass;
    
    // Новые физические свойства
    double friction;        // Коэффициент трения
    double airResistance;   // Сопротивление воздуха
    double elasticity;      // Упругость (0-1)
    double maxVelocity;     // Максимальная скорость
    double angularVelocity; // Угловая скорость
    double rotation;        // Текущий угол поворота

  private:
    State state;
    Hitbox hitbox;
};

// Пространственное разделение для оптимизации коллизий
class SpatialGrid {
public:
    SpatialGrid(double cellSize = 100.0) : cellSize(cellSize) {}
    
    void clear();
    void addEntity(Entity* entity);
    void removeEntity(Entity* entity);
    void updateEntity(Entity* entity);
    std::vector<Entity*> getNearbyEntities(const Entity* entity);
    
private:
    double cellSize;
    std::unordered_map<std::string, std::vector<Entity*>> grid;
    
    std::string getCellKey(const Vec2& position);
    std::vector<std::string> getCellKeys(const Entity* entity);
};

class PhysicsModule {
  public:
    PhysicsModule() = default;
    ~PhysicsModule() = default;

    void addObject(Entity* object);
    void eraseObject(Entity* object);
    void updateObjects(Time_t dt);
    
    // Новые методы для продвинутой физики
    void setGravity(const Vec2& gravity) { this->gravity = gravity; }
    void setGlobalFriction(double friction) { globalFriction = friction; }
    void setGlobalAirResistance(double resistance) { globalAirResistance = resistance; }
    
    // Настройки физики
    void enableAdvancedPhysics(bool enable) { advancedPhysics = enable; }
    void setCollisionIterations(int iterations) { collisionIterations = iterations; }
    
    // Публичные методы для применения импульсов
    void applyImpulse(Entity* obj, const Vec2& impulse);
    void applyAngularImpulse(Entity* obj, double torque);

  private:
    std::vector<Entity*> all_objects;
    SpatialGrid spatialGrid;
    
    // Физические константы
    Vec2 gravity = Vec2(0, -10);
    double globalFriction = 0.0;
    double globalAirResistance = 0.0;
    
    // Настройки
    bool advancedPhysics = true;
    int collisionIterations = 3;
    
    // Приватные методы
    void collideObjects();
    void handleCollision(Entity* obj1, Entity* obj2);
    void applyPhysics(Entity* obj, Time_t dt);
    void updateEntityState(Entity* obj);
    void applyConstraints(Entity* obj);
    void resolveCollision(Entity* obj1, Entity* obj2);
    
    // Упругое столкновение
    void elasticCollision(Entity* obj1, Entity* obj2);
};

