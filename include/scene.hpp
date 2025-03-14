#pragma once

#include "graphics.hpp"
#include "physics.hpp"
#include "textures_config.hpp"

static const uint64_t BG_TEX_ID = hash(BACKGROUND_FILE);
static const uint64_t BB_TEX_ID = hash(BASE_OBJECT_FILE);

class Background : public DrawableSprite {
  public:
  Background(const SpriteManager &sp_man):
    DrawableSprite(sp_man.getTexture(BG_TEX_ID))
  {}
};

class BaseBlock : public GameObject {
  public:
  BaseBlock(const SpriteManager &sp_man, const Vec2& pos_, const Vec2 vel_ = Vec2(0, 0), const double mass_ = 1e9):
    DrawableSprite(sp_man.getTexture(BB_TEX_ID)),
    Entity(Entity::State::FIXED, pos_, vel_, mass_)
  {}
};
