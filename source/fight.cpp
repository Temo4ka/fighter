#include "../include/fight.hpp"

void Fight::restart(const Time_t time) {
    timer = time;

    status = ON_FIGHT;
}

void Fight::mousePressed(MouseContext context) {
    lftPlayer.mousePressed(context);
    rgtPlayer.mousePressed(context);

    return;
}

void Fight::mouseReleased(MouseContext context) {
    lftPlayer.mouseReleased(context);
    rgtPlayer.mouseReleased(context);
    return;
}

void Fight::mouseMoved(MouseContext context) {
    lftPlayer.mouseMoved(context);
    rgtPlayer.mouseMoved(context);
    return;
}

void Fight::keyPressed(KeyboardContext context) {
    lftPlayer.keyPressed(context);
    rgtPlayer.keyPressed(context);

    return;
}

void Fight::keyReleased(KeyboardContext context) {
    lftPlayer.keyReleased(context);
    rgtPlayer.keyReleased(context);

    return;
}

void Fight::timeEvent(Time_t dt) {
    timer -= dt;

    if (timer <= 0) {
        if (lftPlayer.getHP() < rgtPlayer.getHP())
            status = FINISHED_LFT_WON;
        else if (rgtPlayer.getHP() < lftPlayer.getHP())
            status = FINISHED_RGT_WON;
        else
            status = FINISHED_DRAW;
        
        return;
    }

    if (lftPlayer.getHP() <= 0 && rgtPlayer.getHP())
        status = FINISHED_DRAW;
    else if (lftPlayer.getHP() < 0)
        status = FINISHED_LFT_WON;
    else if (rgtPlayer.getHP() < 0)
        status = FINISHED_RGT_WON;

    return;
}