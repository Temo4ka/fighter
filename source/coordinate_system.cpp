#include "../include/coordinate_system.hpp"
#include "../include/DSL.hpp"
#include "../include/config.hpp"
#include <cmath>
#include <chrono>

// Глобальный экземпляр
CoordinateSystem g_coordSystem;

CoordinateSystem::CoordinateSystem() 
    : screenWidth(WINDOW_WID), screenHeight(WINDOW_HGT) {
    updateTransforms();
}

// ==================== Основные методы конвертации ====================

sf::Vector2f CoordinateSystem::worldToScreen(const Vec2& worldPos) const {
    // Применяем трансформацию камеры
    Vec2 cameraOffset = calculateCameraOffset();
    Vec2 relativePos = worldPos - camera.position;
    
    // Применяем масштаб и поворот
    double cosRot = std::cos(-camera.rotation);
    double sinRot = std::sin(-camera.rotation);
    
    Vec2 rotatedPos(
        relativePos.x * cosRot - relativePos.y * sinRot,
        relativePos.x * sinRot + relativePos.y * cosRot
    );
    
    // Масштабируем и центрируем на экране
    Vec2 scaledPos = rotatedPos * camera.zoom;
    Vec2 screenPos = Vec2(scaledPos.x, WINDOW_HGT - scaledPos.y);

    MESSAGE("worldPos(%g, %g)", worldPos.x, worldPos.y);
    MESSAGE("relativePos(%g, %g)", relativePos.x, relativePos.y);
    MESSAGE("scaledPos(%g, %g)", scaledPos.x, scaledPos.y);
    MESSAGE("Rot(%g, %g)", sinRot, cosRot);
    MESSAGE("screenPos(%g, %g)", screenPos.x, screenPos.y);
    
    return sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
}

Vec2 CoordinateSystem::screenToWorld(const sf::Vector2f& screenPos) const {
    // Обратная трансформация
    Vec2 screenVec(screenPos.x, screenPos.y);
    Vec2 centeredPos = screenVec - Vec2(screenWidth / 2.0, screenHeight / 2.0);
    
    // Обратное масштабирование
    Vec2 scaledPos = centeredPos / camera.zoom;
    
    // Обратный поворот
    double cosRot = std::cos(camera.rotation);
    double sinRot = std::sin(camera.rotation);
    
    Vec2 rotatedPos(
        scaledPos.x * cosRot - scaledPos.y * sinRot,
        scaledPos.x * sinRot + scaledPos.y * cosRot
    );
    
    // Добавляем позицию камеры
    Vec2 worldPos = rotatedPos + camera.position;
    
    // Применяем заворачивание мира если включено
    if (worldSettings.wrapWorld) {
        return applyWorldWrap(worldPos);
    }
    
    return worldPos;
}

sf::Vector2f CoordinateSystem::worldToUI(const Vec2& worldPos) const {
    // UI координаты - это экранные координаты без трансформации камеры
    Vec2 screenPos = worldPos * worldSettings.worldScale;
    return sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
}

Vec2 CoordinateSystem::uiToWorld(const sf::Vector2f& uiPos) const {
    // Обратная конвертация UI координат
    Vec2 worldPos(uiPos.x, uiPos.y);
    return worldPos / worldSettings.worldScale;
}

// ==================== Масштабирование ====================

double CoordinateSystem::worldToScreenScale(double worldScale) const {
    return worldScale * camera.zoom * worldSettings.worldScale;
}

double CoordinateSystem::screenToWorldScale(double screenScale) const {
    return screenScale / (camera.zoom * worldSettings.worldScale);
}

// ==================== Управление камерой ====================

void CoordinateSystem::setCameraPosition(const Vec2& pos) {
    camera.position = pos;
    updateTransforms();
}

void CoordinateSystem::setCameraZoom(double zoom) {
    camera.zoom = std::max(0.1, std::min(10.0, zoom)); // Ограничиваем зум
    updateTransforms();
}

void CoordinateSystem::setCameraRotation(double rotation) {
    camera.rotation = rotation;
    updateTransforms();
}

void CoordinateSystem::followTarget(const Vec2& target, double smoothness) {
    camera.target = target;
    // Плавное следование за целью
    Vec2 desiredPos = target;
    Vec2 currentPos = camera.position;
    Vec2 newPos = currentPos + (desiredPos - currentPos) * smoothness;
    setCameraPosition(newPos);
}

void CoordinateSystem::updateCamera(double dt) {
    // Автоматическое обновление камеры
    if (camera.target != Vec2(0, 0)) {
        followTarget(camera.target, 0.05); // Медленное следование
    }
}

// ==================== Настройки мира ====================

void CoordinateSystem::setWorldScale(double scale) {
    worldSettings.worldScale = std::max(1.0, scale);
    updateTransforms();
}

void CoordinateSystem::setWorldBounds(const Vec2& bounds) {
    worldSettings.worldBounds = bounds;
}

void CoordinateSystem::setGravity(const Vec2& gravity) {
    worldSettings.gravity = gravity;
}

// ==================== Геттеры ====================

Vec2 CoordinateSystem::getScreenCenter() const {
    return Vec2(screenWidth / 2.0, screenHeight / 2.0);
}

Vec2 CoordinateSystem::getWorldCenter() const {
    return worldSettings.worldBounds / 2.0;
}

// ==================== Утилиты ====================

bool CoordinateSystem::isInScreen(const Vec2& worldPos) const {
    sf::Vector2f screenPos = worldToScreen(worldPos);
    return screenPos.x >= 0 && screenPos.x <= screenWidth &&
           screenPos.y >= 0 && screenPos.y <= screenHeight;
}

bool CoordinateSystem::isInWorld(const Vec2& worldPos) const {
    return worldPos.x >= 0 && worldPos.x <= worldSettings.worldBounds.x &&
           worldPos.y >= 0 && worldPos.y <= worldSettings.worldBounds.y;
}

void CoordinateSystem::clampToWorld(Vec2& worldPos) const {
    worldPos.x = std::max(0.0, std::min(worldSettings.worldBounds.x, worldPos.x));
    worldPos.y = std::max(0.0, std::min(worldSettings.worldBounds.y, worldPos.y));
}

// ==================== Приватные методы ====================

void CoordinateSystem::updateTransforms() {
    // Обновляем матрицы трансформации для оптимизации
    worldToScreenTransform = sf::Transform::Identity;
    worldToScreenTransform.translate(sf::Vector2f(screenWidth / 2.0f, screenHeight / 2.0f));
    worldToScreenTransform.scale(sf::Vector2f(static_cast<float>(camera.zoom), static_cast<float>(camera.zoom)));
    worldToScreenTransform.rotate(sf::degrees(static_cast<float>(-camera.rotation * 180.0 / M_PI)));
    worldToScreenTransform.translate(sf::Vector2f(static_cast<float>(-camera.position.x), static_cast<float>(-camera.position.y)));
    
    // Обратная матрица
    screenToWorldTransform = worldToScreenTransform.getInverse();
}

Vec2 CoordinateSystem::applyWorldWrap(const Vec2& pos) const {
    Vec2 wrappedPos = pos;
    
    // Заворачиваем по X
    if (wrappedPos.x < 0) {
        wrappedPos.x += worldSettings.worldBounds.x;
    } else if (wrappedPos.x > worldSettings.worldBounds.x) {
        wrappedPos.x -= worldSettings.worldBounds.x;
    }
    
    // Заворачиваем по Y
    if (wrappedPos.y < 0) {
        wrappedPos.y += worldSettings.worldBounds.y;
    } else if (wrappedPos.y > worldSettings.worldBounds.y) {
        wrappedPos.y -= worldSettings.worldBounds.y;
    }
    
    return wrappedPos;
}

Vec2 CoordinateSystem::calculateCameraOffset() const {
    // Вычисляем смещение камеры для центрирования
    return Vec2(screenWidth / 2.0, screenHeight / 2.0);
}
