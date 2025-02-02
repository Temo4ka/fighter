#pragma once

#include "fighters.hpp"

class Fight {
  public:
    Fight() = default;

    explicit Fight(FighterInfo &leftPlayerInfo, FighterInfo &rightPlayerInfo):
    leftPlayer(leftPlayerInfo),
    rightPlayer(rightPlayerInfo)
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
    Fighter rightPlayer;
    Fighter  leftPlayer;
};


class FightInfo  {
  public:
    FightInfo() = delete;

    FightInfo(FighterInfo &leftPlayerInfo_, FighterInfo &rightPlayerInfo_):
    leftPlayerInfo  (leftPlayerInfo_ ),
    rightPlayerInfo (rightPlayerInfo_)
    {}

  private:
    FighterInfo  leftPlayerInfo;
    FighterInfo rightPlayerInfo;
}

