#pragma once

#include "physics.hpp"
#include "graphics.hpp"
#include "coordinate_system.hpp"
#include <cmath>

class GameObject : public DrawableSprite, public Entity {
  public:

  GameObject(sf::Texture *texture, const Vec2 &pos_, const Vec2 &scale_, const Entity::State &state_, 
                                                                                const Vec2 &vel_ = Vec2(0, 0), const double mass_ = BASE_MASS):
    DrawableSprite(texture, sf::Vector2f(pos_.x, pos_.y), sf::Vector2f(scale_.x, scale_.y)),
    Entity (state_, pos_, scale_, vel_, mass_)
  {
    // Инициализируем графическую позицию
    updateGraphicsPosition();
  }

  // Автоматическая синхронизация физики с графикой
  void updateGraphicsPosition() {
    // Конвертируем мировые координаты в экранные
    sf::Vector2f screenPos = toScreen(position);
    sf::Vector2f screenScale = sf::Vector2f(
        static_cast<float>(scale.x), 
        static_cast<float>(scale.y)
    );
    
    // Используем методы DrawableSprite для графики
    DrawableSprite::setPosition(screenPos);
    DrawableSprite::setScale(screenScale);
    
    // Обновляем вращение если есть
    if (rotation != 0.0) {
        // SFML использует градусы, а не радианы
        float degrees = static_cast<float>(rotation * 180.0 / M_PI);
        DrawableSprite::setRotation(degrees);
    }
  }

  // Переопределяем методы Entity для автоматической синхронизации
  void setWorldPosition(const Vec2& newPos) {
    Entity::position = newPos;
    updateGraphicsPosition();
  }
  
  void setWorldScale(const Vec2& newScale) {
    Entity::scale = newScale;
    updateGraphicsPosition();
  }
  
  void setWorldRotation(double newRotation) {
    Entity::rotation = newRotation;
    updateGraphicsPosition();
  }


  // Проверка видимости объекта на экране
  bool isVisibleOnScreen() const {
    return g_coordSystem.isInScreen(position);
  }

  // Получение экранных координат
  sf::Vector2f getScreenPosition() const {
    return toScreen(position);
  }

  // Получение экранного масштаба
  sf::Vector2f getScreenScale() const {
    return sf::Vector2f(
        static_cast<float>(scale.x * g_coordSystem.getWorldSettings().worldScale),
        static_cast<float>(scale.y * g_coordSystem.getWorldSettings().worldScale)
    );
  }

  private:
};