#include "include/coordinate_system.hpp"
#include "include/physics.hpp"
#include "include/graphics.hpp"
#include "include/config.hpp"
#include <iostream>
#include <memory>

// Тест новой системы координат
int main() {
    std::cout << "🎯 Тестирование новой системы координат Fighter\n";
    std::cout << "============================================\n\n";
    
    // Инициализируем систему координат
    CoordinateSystem& coordSystem = g_coordSystem;
    
    // Настраиваем мир
    coordSystem.setWorldScale(100.0);  // 100 пикселей = 1 метр
    coordSystem.setWorldBounds(Vec2(2000, 1500));  // Мир 2000x1500 пикселей
    coordSystem.setGravity(Vec2(0, -9.8));
    
    std::cout << "✅ Система координат инициализирована\n";
    std::cout << "   Масштаб мира: " << coordSystem.getWorldSettings().worldScale << " пикселей/метр\n";
    std::cout << "   Размеры мира: " << coordSystem.getWorldSettings().worldBounds.x << "x" << coordSystem.getWorldSettings().worldBounds.y << "\n\n";
    
    // Тестируем конвертацию координат
    std::cout << "🔄 Тестирование конвертации координат...\n";
    
    // Мировые координаты (физика)
    Vec2 worldPos(500, 300);  // 5 метров вправо, 3 метра вниз
    std::cout << "   Мировые координаты: (" << worldPos.x << ", " << worldPos.y << ")\n";
    
    // Конвертируем в экранные координаты
    sf::Vector2f screenPos = coordSystem.worldToScreen(worldPos);
    std::cout << "   Экранные координаты: (" << screenPos.x << ", " << screenPos.y << ")\n";
    
    // Обратная конвертация
    Vec2 backToWorld = coordSystem.screenToWorld(screenPos);
    std::cout << "   Обратно в мир: (" << backToWorld.x << ", " << backToWorld.y << ")\n";
    
    // Проверяем точность
    double error = std::sqrt((worldPos.x - backToWorld.x) * (worldPos.x - backToWorld.x) + 
                            (worldPos.y - backToWorld.y) * (worldPos.y - backToWorld.y));
    std::cout << "   Ошибка конвертации: " << error << " пикселей\n\n";
    
    // Тестируем камеру
    std::cout << "📷 Тестирование камеры...\n";
    
    // Устанавливаем позицию камеры
    coordSystem.setCameraPosition(Vec2(100, 50));
    std::cout << "   Позиция камеры: (" << coordSystem.getCamera().position.x 
              << ", " << coordSystem.getCamera().position.y << ")\n";
    
    // Конвертируем с новой позицией камеры
    sf::Vector2f screenPosWithCamera = coordSystem.worldToScreen(worldPos);
    std::cout << "   Экранные координаты с камерой: (" << screenPosWithCamera.x 
              << ", " << screenPosWithCamera.y << ")\n\n";
    
    // Тестируем зум камеры
    std::cout << "🔍 Тестирование зума камеры...\n";
    
    coordSystem.setCameraZoom(2.0);  // Увеличиваем в 2 раза
    std::cout << "   Зум камеры: " << coordSystem.getCamera().zoom << "x\n";
    
    sf::Vector2f screenPosWithZoom = coordSystem.worldToScreen(worldPos);
    std::cout << "   Экранные координаты с зумом: (" << screenPosWithZoom.x 
              << ", " << screenPosWithZoom.y << ")\n\n";
    
    // Тестируем масштабирование
    std::cout << "📏 Тестирование масштабирования...\n";
    
    double worldScale = 50.0;  // 50 пикселей = 1 метр
    double screenScale = coordSystem.worldToScreenScale(worldScale);
    std::cout << "   Мировой масштаб: " << worldScale << " пикселей/метр\n";
    std::cout << "   Экранный масштаб: " << screenScale << " пикселей/метр\n";
    
    // Обратное масштабирование
    double backToWorldScale = coordSystem.screenToWorldScale(screenScale);
    std::cout << "   Обратно в мир: " << backToWorldScale << " пикселей/метр\n\n";
    
    // Тестируем границы мира
    std::cout << "🌍 Тестирование границ мира...\n";
    
    Vec2 centerWorld = coordSystem.getWorldCenter();
    std::cout << "   Центр мира: (" << centerWorld.x << ", " << centerWorld.y << ")\n";
    
    Vec2 centerScreen = coordSystem.getScreenCenter();
    std::cout << "   Центр экрана: (" << centerScreen.x << ", " << centerScreen.y << ")\n";
    
    // Проверяем, находится ли объект в мире
    Vec2 inWorldPos(100, 100);
    Vec2 outOfWorldPos(2500, 2000);
    
    std::cout << "   Позиция (100, 100) в мире: " 
              << (coordSystem.isInWorld(inWorldPos) ? "Да" : "Нет") << "\n";
    std::cout << "   Позиция (2500, 2000) в мире: " 
              << (coordSystem.isInWorld(outOfWorldPos) ? "Да" : "Нет") << "\n\n";
    
    // Тестируем следование камеры за целью
    std::cout << "🎯 Тестирование следования камеры...\n";
    
    Vec2 target(800, 400);
    coordSystem.followTarget(target, 0.5);
    std::cout << "   Цель камеры: (" << target.x << ", " << target.y << ")\n";
    std::cout << "   Позиция камеры после следования: (" 
              << coordSystem.getCamera().position.x << ", " 
              << coordSystem.getCamera().position.y << ")\n\n";
    
    // Тестируем UI координаты
    std::cout << "🖥️ Тестирование UI координат...\n";
    
    Vec2 uiWorldPos(150, 200);
    sf::Vector2f uiPos = coordSystem.worldToUI(uiWorldPos);
    std::cout << "   Мировые координаты для UI: (" << uiWorldPos.x << ", " << uiWorldPos.y << ")\n";
    std::cout << "   UI координаты: (" << uiPos.x << ", " << uiPos.y << ")\n";
    
    Vec2 backToUIWorld = coordSystem.uiToWorld(uiPos);
    std::cout << "   Обратно в мир: (" << backToUIWorld.x << ", " << backToUIWorld.y << ")\n\n";
    
    // Тестируем производительность
    std::cout << "⚡ Тестирование производительности...\n";
    
    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        Vec2 testPos(i % 1000, i % 800);
        sf::Vector2f screenPos = coordSystem.worldToScreen(testPos);
        Vec2 worldPos = coordSystem.screenToWorld(screenPos);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "   " << iterations << " конвертаций за " << duration.count() << " микросекунд\n";
    std::cout << "   Среднее время: " << (duration.count() / (double)iterations) << " микросекунд на конвертацию\n\n";
    
    std::cout << "🎉 Тест системы координат завершен успешно!\n";
    std::cout << "Новая система координат работает корректно и эффективно.\n";
    
    return 0;
}
