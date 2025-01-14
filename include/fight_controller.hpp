#pragma <once>

#include <cstdint>
#include <string>

#include "fighters.hpp"
#include "event.hpp"

class FightController {
  public:
    FightController() = delete;

  private:
    enum Status {
        Stopped = 0,
        Fighting = 1,
        Paused = 2,
        Final = 3,
    } currentStatus;

    Fight currentFight;

    Menu pauseMenu;

    uint8_t currentRoundNum;

    EventManager& eventMan;
};

class Fight {
  public:
    Fight() = default;

    void startFight(FighterInfo &leftPlayerInfo, FighterInfo &rightPlayerInfo)
    leftPlayer(leftPlayerInfo),
    rightPlayer(rightPlayerInfo)
    {}

  private:
    Fighter rightPlayer;
    Fighter  leftPlayer;
};