#pragma once

#include "fighters.hpp"

class Fight {
  public:
    Fight() = default;

    explicit Fight(FighterInfo &lftPlayerInfo, FighterInfo &rgtPlayerInfo):
		lftPlayer (lftPlayerInfo),
		rgtPlayer (rgtPlayerInfo)
    {}

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

