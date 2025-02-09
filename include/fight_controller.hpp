#pragma once

#include <cstdint>
#include <string>

#include "config.hpp"  
#include "event.hpp"
#include "fight.hpp"
#include "gui.hpp"

class FightController {
  public:
    FightController() = delete; // mb it's not essential

    explicit FightController(PhysicsModule &physModule_, GraphicsModule &graphModule_,
                                FighterInfo &lftPlayerInfo, FighterInfo &rgtPlayerInfo):
        physModule (physModule_),
        graphModule (graphModule_),
        currentFight(lftPlayerInfo, rgtPlayerInfo)
    {} 

    void startFight(const FighterInfo &fighter1, const FighterInfo &fighter2);

    void restartFight();

    void stopFight();

    //--------Events-------------------
    void  mousePressed(MouseContext context);
    void mouseReleased(MouseContext context);
    void    mouseMoved(MouseContext context);

    void  keyPressed(KeyboardContext context);
    void keyReleased(KeyboardContext context);

    void timeEvent(Time_t dt);
    //=================================

  private:
    enum Status {
        Stopped = 0,
        Fighting = 1,
        Paused = 2,
        Final = 3,
    } currentStatus;

    Fight currentFight;

    // Menu PauseMenu;
    // Menu FinalMenu;

    uint8_t currentRoundNum;

    PhysicsModule &physModule;
    GraphicsModule &graphModule;
};

