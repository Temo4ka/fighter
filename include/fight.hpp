#pragma once

#include "fighters.hpp"
#include "scene.hpp"

class Fight {
  public:
    Fight() = delete;

    explicit Fight(FighterInfo &lftPlayerInfo, FighterInfo &rgtPlayerInfo, const Time_t time = TIME):
		lftPlayer (lftPlayerInfo),
		rgtPlayer (rgtPlayerInfo),
        timer     (time),
        status    (ON_FIGHT)
    {
      // Basic Scene ( objects.push_back(...); )
      Background background();
      BaseBlock base_block(Vec2(0, WINDOW_HGT / 2));

      objects.push_back(base_block);
    }

    //TODO: Load Scene from txt file

    std::vector<Entity>& getObjects() { return objects; }

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

    std::vector<Entity> objects;

    Time_t  timer;
};


