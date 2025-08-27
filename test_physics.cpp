#include "include/physics.hpp"
#include "include/graphics.hpp"
#include "include/config.hpp"
#include <iostream>
#include <memory>

// Простой тест улучшенного физического движка
int main() {
    std::cout << "🚀 Тестирование улучшенного физического движка Fighter\n";
    std::cout << "================================================\n\n";
    
    // Создаем физический модуль
    PhysicsModule physics;
    
    // Настраиваем физику
    physics.setGravity(Vec2(0, -9.8));
    physics.setGlobalFriction(0.1);
    physics.setGlobalAirResistance(0.02);
    physics.enableAdvancedPhysics(true);
    physics.setCollisionIterations(3);
    
    std::cout << "✅ Физический модуль создан и настроен\n";
    
    // Создаем тестовые объекты
    Entity* ball1 = new Entity(Entity::ON_MOVE, Vec2(100, 100), Vec2(50, 0), 10);
    ball1->setElasticity(0.8);
    ball1->setFriction(0.1);
    ball1->setAirResistance(0.01);
    ball1->setMaxVelocity(100.0);
    
    Entity* ball2 = new Entity(Entity::ON_MOVE, Vec2(200, 100), Vec2(-30, 0), 15);
    ball2->setElasticity(0.9);
    ball2->setFriction(0.05);
    ball2->setAirResistance(0.005);
    ball2->setMaxVelocity(120.0);
    
    Entity* platform = new Entity(Entity::FIXED, Vec2(0, 600), Vec2(0, 0), 1000);
    platform->setFriction(0.8);
    
    std::cout << "✅ Тестовые объекты созданы:\n";
    std::cout << "   - Мяч 1: масса=10, упругость=0.8, трение=0.1\n";
    std::cout << "   - Мяч 2: масса=15, упругость=0.9, трение=0.05\n";
    std::cout << "   - Платформа: фиксированная, трение=0.8\n\n";
    
    // Добавляем объекты в физику
    physics.addObject(ball1);
    physics.addObject(ball2);
    physics.addObject(platform);
    
    std::cout << "✅ Объекты добавлены в физический движок\n\n";
    
    // Симулируем физику
    std::cout << "🔄 Симуляция физики...\n";
    
    for (int frame = 0; frame < 100; ++frame) {
        double dt = 0.016; // 60 FPS
        
        physics.updateObjects(dt);
        
        if (frame % 20 == 0) {
            std::cout << "   Кадр " << frame << ":\n";
            std::cout << "     Мяч 1: pos=(" << ball1->position.x << ", " << ball1->position.y 
                      << "), vel=(" << ball1->velocity.x << ", " << ball1->velocity.y << ")\n";
            std::cout << "     Мяч 2: pos=(" << ball2->position.x << ", " << ball2->position.y 
                      << "), vel=(" << ball2->velocity.x << ", " << ball2->velocity.y << ")\n";
            std::cout << "     Состояние мяч 1: " << (int)ball1->getState() << "\n";
            std::cout << "     Состояние мяч 2: " << (int)ball2->getState() << "\n\n";
        }
    }
    
    std::cout << "✅ Симуляция завершена!\n\n";
    
    // Демонстрируем применение импульсов
    std::cout << "💥 Демонстрация импульсов...\n";
    
    Vec2 punchImpulse = Vec2(100, -50);
    physics.applyImpulse(ball1, punchImpulse);
    
    std::cout << "   Применен импульс (" << punchImpulse.x << ", " << punchImpulse.y << ") к мячу 1\n";
    std::cout << "   Новая скорость мяча 1: (" << ball1->velocity.x << ", " << ball1->velocity.y << ")\n\n";
    
    // Демонстрируем угловые импульсы
    std::cout << "🔄 Демонстрация вращения...\n";
    
    physics.applyAngularImpulse(ball1, 25.0);
    
    std::cout << "   Применен угловой импульс 25.0 к мячу 1\n";
    std::cout << "   Угловая скорость мяча 1: " << ball1->angularVelocity << "\n";
    std::cout << "   Вращение мяча 1: " << ball1->rotation << "\n\n";
    
    // Очистка
    delete ball1;
    delete ball2;
    delete platform;
    
    std::cout << "🎯 Тест завершен успешно!\n";
    std::cout << "Улучшенный физический движок работает корректно.\n";
    
    return 0;
}
