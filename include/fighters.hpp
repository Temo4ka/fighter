#pragma once

#include <cstdint>
#include <map>
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

    void setMouseControls(std::map<Key, Commands> &map) { KeyControls = map; }

    void setKeyControls(std::map<MouseButton, Commands> &map) { MouseControls = map; }

    enum Commands {
        UNDEFINED = 0
        MOVE_LEFT,
        MOVE_RIGHT,
        JUMP,
        SIT,
        ATTACK_WITH_HAND,
        ATTACK_WITH_LEG
    };

    Command getCommand(const Key &key) const { return KeyControls[key]; }
    Command getCommand(const MouseButton &m_button) const { return MouseControls[m_button]; }

  private:
    uint8_t strength;
    uint8_t dexterity;
    uint8_t constitution;
    //uint8_t intelligence; ?

    //TODO: Inventary mb?

    std::map<Key, Commands> KeyControls = { {Key::A, Commands::MOVE_LEFT }, 
                                            {Key::D, Commands::MOVE_RIGHT}, 
                                            {Key::S, Commnads::SIT},
                                            {Key::W, Commands::JUMP}, 
                                            {Key::Space, Commands::JUMP},
                                            {Key::Q, Commands::ATTACK_WITH_HAND}, 
                                            {Key::E, Commands::ATTACK_WITH_LEG}
                                          };

    std::map<MouseButton, Commands> MouseControls = {};
};

class Fighter : public DrawableSprite, public Entity {
  public:
    Fighter() = delete;
    
    Fighter(const SpriteInfo &spInfo, const FighterInfo &fgtrInfo, const Vec2 &pos_, const Entity::State &state_):
      DrawableSprite(spInfo),
      Entity (state_, pos_, Vec2(0, 0)),
      fighterStartParams (fgtrInfo)
    {}

    //---Events---------------------------------
    void  mousePressed(MouseContext context);
    void mouseReleased(MouseContext context);
    void    mouseMoved(MouseContext context);

    void  keyPressed(KeyboardContext context);
    void keyReleased(KeyboardContext context);
    //===========================================

    //---Commands--------------------------------
    void sit();
    void jump();
    void move_left();
    void move_right();
    void attack_with_hand();
    void attack_with_leg();
    //===========================================

    void timeEvent(Time_t dt);

  private:
    uint8_t hp; // percantage mb?

    FighterInfo fighterStartParams;
};