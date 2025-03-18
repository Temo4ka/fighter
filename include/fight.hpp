#pragma once

#include "game_object.hpp"
#include "fighters.hpp"
#include "scene.hpp"

class Fight {
  public:
    Fight() = delete;

    explicit Fight(GraphicsModule &g_module, const FighterInfo &lftPlayerInfo, const FighterInfo &rgtPlayerInfo, const Time_t time = TIME):
		  lftPlayer (lftPlayerInfo),
		  rgtPlayer (rgtPlayerInfo),
      sp_man       (g_module.getSpriteManager()),
      status         (ON_FIGHT),
      timer              (time)
    {
      // Basic Scene ( objects.push_back(...); )
      BaseBlock *base_block = new BaseBlock(g_module.getSpriteManager(), Vec2(0, WINDOW_HGT / 2));
      
      objects.push_back(base_block);

      g_module.insertObject(new Background(g_module.getSpriteManager()));
      g_module.insertObject(base_block);
    }

    //TODO: Load Scene from txt file

    std::vector<GameObject*>& getObjects() { return objects; }

    Fighter* getLftFighter() { return &lftPlayer; }
    Fighter* getRgtFighter() { return &rgtPlayer; }

    void restart(const Time_t time = TIME);

    //--------Events-------------------
    void  mousePressed(MouseContext context);
    void mouseReleased(MouseContext context);
    void    mouseMoved(MouseContext context);

    void  keyPressed(KeyboardContext context);
    void keyReleased(KeyboardContext context);

    void timeEvent(Time_t dt);
    //=================================

    enum Status {
        UNCONSTRUCTED,
        ON_FIGHT,
        FINISHED_LFT_WON,
        FINISHED_RGT_WON,
        FINISHED_DRAW
    } status = UNCONSTRUCTED;

  private:
    Fighter rgtPlayer;
    Fighter lftPlayer;

    std::vector<GameObject*> objects;

    Time_t  timer;

    SpriteManager &sp_man;
};


