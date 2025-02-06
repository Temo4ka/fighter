#pragma once

#include <cstdint>
#include <string>

#include "event.hpp"
#include "physics.hpp"
#include "graphics.hpp"

class FighterInfo {
    FighterInfo() = delete;

    FighterInfo(uint8_t &strength_, uint8_t &dexterity_, uint8_t &constitution_):
    strength(strength_),
    dexterity(dexterity_),
    constitution(constitution_)
    {}

  private:
    uint8_t strength;
    uint8_t dexterity;
    uint8_t constitution;
    //uint8_t intelligence; ?

    //TODO: Inventary mb?

    enum Commands {
        MOVE_LEFT,
        MOVE_RIGHT,
        JUMP,
        SIT,
        ATTACK_WITH_HAND,
        ATTACK_WITH_LEG
    };

    std::map<Key, Commands> Controls = { {Key::A, Commands::MOVE_LEFT }, 
                                         {Key::D, Commands::MOVE_RIGHT}, 
                                         {Key::S, Commnads::SIT},
                                         {Key::W, Commands::JUMP}, 
                                         {Key::Space, Commands::JUMP},
                                         {Key::Q, Commands::ATTACK_WITH_HAND}, 
                                         {Key::E, Commands::ATTACK_WITH_LEG}
                                        };
};

class Fighter : public DrawableSprite, public Entity {
  public:
    Fighter() = delete;
    
    Fighter(SpriteInfo &spInfo, FighterInfo &fgtrInfo):
      fighterStartParams (fgtrInfo),
      Drawable()

    void  mousePressed(MouseContext context);
    void mouseReleased(MouseContext context);
    void    mouseMoved(MouseContext context);

    void  keyPressed(KeyboardContext context);
    void keyReleased(KeyboardContext context);

    void timeEvent(Time_t dt);

  private:
    uint8_t hp; // percantage mb?

    FighterInfo fighterStartParams;
};