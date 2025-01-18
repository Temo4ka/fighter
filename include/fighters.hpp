#pragma once

#include <cstdint>
#include <string>

#include "graphics.hpp"

struct FighterInfo {
    FighterInfo() = delete;

    FighterInfo(uint8_t &strength_, uint8_t &dexterity_, uint8_t &constitution_):
    strength(strength_),
    dexterity(dexterity_),
    constitution(constitution_)
    {}

    uint8_t strength;
    uint8_t dexterity;
    uint8_t constitution;
    //uint8_t intelligence; ?

    std::string asset_path;

    //TODO: Inventary mb?
};

class Fighter : public Drawable {
  public:
    Fighter() {}
    
    Fighter(FighterInfo &info) {}

    //void draw();

  private:
    
};