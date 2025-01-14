#pragma <once>

#include <cstdint>
#include "fighters.hpp"
#include "event.hpp"

class Fight {
  public:

  private:
    Fighter* rightPlayer;
    Fighter*  leftPlayer;
};

class FightController {
  public:
    FightController() {}

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