#pragma <once>

#include <cstdint>
#include <string>

#include "event.hpp"
#include "fighters.hpp"
#include "gui.hpp"

class FightController {
  public:
    FightController() = delete;

    void startFight(FighterInfo &fighter1, FighterInfo &fighter2);

    void restartFight();

    void stopFight();

    //--------Events-------------------

    //=================================

  private:
    enum Status {
        Stopped = 0,
        Fighting = 1,
        Paused = 2,
        Final = 3,
    } currentStatus;

    Fight currentFight;

    Menu PauseMenu;
    Menu FinalMenu;

    uint8_t currentRoundNum;

    EventManager& eventMan;

};

class Fight {
  public:
    Fight() = default;

    Fight(FighterInfo &leftPlayerInfo, FighterInfo &rightPlayerInfo)
    leftPlayer(leftPlayerInfo),
    rightPlayer(rightPlayerInfo)
    {}

    //--------Events-------------------

    //=================================

  private:
    Fighter rightPlayer;
    Fighter  leftPlayer;
};