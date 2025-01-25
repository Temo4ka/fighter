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