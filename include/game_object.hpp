#pragma once

#include "physics.hpp"
#include "graphics.hpp"

class GameObject : public DrawableSprite, public Entity {
  public:

  GameObject(const sf::Texture texture, const Vec2 &pos_, const Vec2 &scale_, const Entity::State &state_):
    DrawableSprite(texture, sf::Vector2f(pos_.x, pos_.y), sf::Vector2f(scale_.x, scale_.y)),
    Entity (state_, pos_, Vec2(0, 0), BASE_MASS)
  {}

  void applyPhysToGraph() {
    setPosition(sf::Vector2f(position.x, position.y));
  }

  private:
};