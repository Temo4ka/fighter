#pragma once

#include "vec2.hpp"
#include <SFML/Graphics.hpp>

// Система координат для разделения физического мира и экрана
class CoordinateSystem {
public:
    // Координатные пространства
    enum Space {
        WORLD,      // Мировые координаты (физика)
        SCREEN,     // Экранные координаты (графика)
        UI          // Координаты пользовательского интерфейса
    };
    
    // Настройки камеры
    struct Camera {
        Vec2 position;      // Позиция камеры в мировых координатах
        double zoom;         // Масштаб (1.0 = 100%)
        double rotation;     // Поворот камеры
        Vec2 target;         // Цель камеры (для следования за объектом)
        
        Camera() : position(0, 0), zoom(1.0), rotation(0.0), target(0, 0) {}
    };
    
    // Настройки мира
    struct WorldSettings {
        double worldScale;           // Масштаб мира (пикселей на метр)
        Vec2 worldBounds;            // Границы мира
        Vec2 gravity;                // Гравитация в мировых координатах
        bool wrapWorld;              // Заворачивать ли мир по краям
        
        WorldSettings() : worldScale(100.0), worldBounds(2000, 1500), 
                         gravity(0, -9.8), wrapWorld(false) {}
    };
    
    CoordinateSystem();
    ~CoordinateSystem() = default;
    
    // Основные методы конвертации
    sf::Vector2f worldToScreen(const Vec2& worldPos) const;
    Vec2 screenToWorld(const sf::Vector2f& screenPos) const;
    sf::Vector2f worldToUI(const Vec2& worldPos) const;
    Vec2 uiToWorld(const sf::Vector2f& uiPos) const;
    
    // Масштабирование
    double worldToScreenScale(double worldScale) const;
    double screenToWorldScale(double screenScale) const;
    
    // Управление камерой
    void setCameraPosition(const Vec2& pos);
    void setCameraZoom(double zoom);
    void setCameraRotation(double rotation);
    void followTarget(const Vec2& target, double smoothness = 0.1);
    void updateCamera(double dt);
    
    // Настройки мира
    void setWorldScale(double scale);
    void setWorldBounds(const Vec2& bounds);
    void setGravity(const Vec2& gravity);
    
    // Геттеры
    const Camera& getCamera() const { return camera; }
    const WorldSettings& getWorldSettings() const { return worldSettings; }
    Vec2 getScreenCenter() const;
    Vec2 getWorldCenter() const;
    
    // Утилиты
    bool isInScreen(const Vec2& worldPos) const;
    bool isInWorld(const Vec2& worldPos) const;
    void clampToWorld(Vec2& worldPos) const;
    
private:
    Camera camera;
    WorldSettings worldSettings;
    
    // Размеры экрана
    int screenWidth;
    int screenHeight;
    
    // Матрицы трансформации
    sf::Transform worldToScreenTransform;
    sf::Transform screenToWorldTransform;
    
    // Обновление матриц трансформации
    void updateTransforms();
    
    // Вспомогательные методы
    Vec2 applyWorldWrap(const Vec2& pos) const;
    Vec2 calculateCameraOffset() const;
};

// Глобальный экземпляр системы координат
extern CoordinateSystem g_coordSystem;

// Утилитарные функции для быстрого доступа
inline sf::Vector2f toScreen(const Vec2& worldPos) {
    return g_coordSystem.worldToScreen(worldPos);
}

inline Vec2 toWorld(const sf::Vector2f& screenPos) {
    return g_coordSystem.screenToWorld(screenPos);
}

inline double toScreenScale(double worldScale) {
    return g_coordSystem.worldToScreenScale(worldScale);
}

inline double toWorldScale(double screenScale) {
    return g_coordSystem.screenToWorldScale(screenScale);
}
