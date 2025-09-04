#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "game_object.hpp"
#include "event.hpp"
#include "physics.hpp"
#include "graphics.hpp"

class FighterInfo {
  public:
    enum Commands {
        UNDEFINED = 0,
        MOVE_LEFT,
        MOVE_RIGHT,
        JUMP,
        SIT,
        ATTACK_WITH_HAND,
        ATTACK_WITH_LEG
    };

    FighterInfo() = delete;

    FighterInfo(SpriteManager &sprite_man_, uint64_t textureID_, uint8_t strength_, uint8_t dexterity_, uint8_t constitution_):
      textureID(textureID_),  
      sprite_man(sprite_man_),
      strength(strength_),
      dexterity(dexterity_),
      constitution(constitution_)
    {}

    void setMouseControls(std::map<Key, Commands> &map) { KeyControls = map; }

    void setKeyControls(std::map<MouseButton, Commands> &map) { MouseControls = map; }

    Commands getCommand(const Key &key) { return KeyControls[key]; }
    Commands getCommand(const MouseButton &m_button) { return MouseControls[m_button]; }

    sf::Texture *getTexture() const { return sprite_man.getTexture(textureID); }

  private:
    SpriteManager &sprite_man;
    uint64_t textureID; 

    uint8_t strength;
    uint8_t dexterity;
    uint8_t constitution;
    //uint8_t intelligence; ?

    //TODO: Inventary mb?

    std::map<Key, Commands> KeyControls = { {Key::A, Commands::MOVE_LEFT }, 
                                            {Key::D, Commands::MOVE_RIGHT}, 
                                            {Key::S, Commands::SIT},
                                            {Key::W, Commands::JUMP}, 
                                            {Key::Space, Commands::JUMP},
                                            {Key::Q, Commands::ATTACK_WITH_HAND}, 
                                            {Key::E, Commands::ATTACK_WITH_LEG}
                                          };

    std::map<MouseButton, Commands> MouseControls = {};
};

class Fighter : public GameObject {
  public:
    Fighter() = delete;
    
    Fighter(const FighterInfo &fgtrInfo, const Vec2 &pos_ = Vec2(0, 0), const Vec2 &scale_ = Vec2(1, 1),
            const Entity::State &state_ = Entity::State::IN_AIR):
      GameObject(fgtrInfo.getTexture(), pos_, scale_, state_),
      fighterStartParams (fgtrInfo)
    {}

    uint8_t getHP() const { return hp;}

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