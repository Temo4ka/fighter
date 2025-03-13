#pragma once

#include "graphics.hpp"
#include "physics.hpp"
#include "textures_config.hpp"

class Background : public DrawableSprite {
  public:

  static const uint64_t textureID = hash(BACKGROUND_FILE);
    
  Background(const SpriteManager &sp_man):
    DrawableSprite(sp_man.getTexture(id))
  {}
};

class BaseBlock : public DrawableSprite, public Entity {
  public:

  static const uint64_t textureID = hash(BASE_OBJECT_FILE);
    
  BaseBlock(const SpriteManager &sp_man, const Vec2& pos_, const Vec2 vel_ = Vec2(0, 0), const double mass_ = 1e9):
    DrawableSprite(sp_man.getTexture(id)),
    Entity(Entity::State::FIXED, pos_, vel_, mass_)
  {}
};
