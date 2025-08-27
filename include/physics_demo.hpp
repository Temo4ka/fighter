#pragma once

#include "physics.hpp"
#include "graphics.hpp"
#include <memory>

// Демонстрация улучшенного физического движка
class PhysicsDemo {
public:
    PhysicsDemo();
    ~PhysicsDemo() = default;
    
    void run();
    void createDemoScene();
    void update(Time_t dt);
    void render();
    
private:
    std::unique_ptr<PhysicsModule> physics;
    std::unique_ptr<GraphicsModule> graphics;
    
    // Демо объекты
    std::vector<std::unique_ptr<Entity>> demoEntities;
    
    // Настройки демо
    void setupPhysics();
    void createBouncingBalls();
    void createSlidingObjects();
    void createElasticCollisions();
    
    // Интерактивность
    void handleInput();
    void addRandomObject();
    void togglePhysicsMode();
    
    bool advancedMode = true;
    int demoStep = 0;
};
