#pragma once

#include "fighters.hpp"

class Fight {
  public:
    Fight() = default;

    explicit Fight(FighterInfo &lftPlayerInfo, FighterInfo &rgtPlayerInfo, const Time_t time = TIME):
		lftPlayer (lftPlayerInfo),
		rgtPlayer (rgtPlayerInfo),
    timer     (time)
    {
      // Basic Scene ( objects.push_back(...); )
    }

    Fight& operator=(Fight&& fight) = default;

    //TODO: Load Scene from txt file

    std::vector<Entity>& getObjects() const { return objects; }

    Fighter* getLftFighter() const { return lftPlayer; }
    Fighter* getRgtFighter() const { return rgtPlayer; }

    void restart(const Time_t time = TIME);

    //--------Events-------------------
    void  mousePressed(MouseContext context);
    void mouseReleased(MouseContext context);
    void    mouseMoved(MouseContext context);

    void  keyPressed(KeyboardContext context);
    void keyReleased(KeyboardContext context);

    void timeEvent(Time_t dt);
    //=================================

  private:
    Fighter rgtPlayer;
    Fighter lftPlayer;

    std::vector<Entity> objects;

    Time_t  timer;
};


class FightInfo  {
  public:
    FightInfo() = delete;

    FightInfo(FighterInfo &lftPlayerInfo_, FighterInfo &rgtPlayerInfo_):
        lftPlayerInfo (lftPlayerInfo_),
        rgtPlayerInfo (rgtPlayerInfo_)
    {}

  private:
    FighterInfo  lftPlayerInfo;
    FighterInfo rgtPlayerInfo;
}

