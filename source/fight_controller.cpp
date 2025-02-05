#include "../include/fight_controller.hpp"

void FightController::mousePressed(MouseContext context) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->mousePressed(context);
            break;

        case Final:
            FinalMenu->mousePressed(context);
            break;

        case Paused:
            PauseMenu->mousePressed(context);
            break;

        case Stopped:
        case default:
            break;
    }

    return;
}

void FightController::mouseReleased(MouseContext context) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->mouseReleased(context);
            break;
            
        case Final:
            FinalMenu->mouseReleased(context);
            break;

        case Paused:
            PauseMenu->mouseReleased(context);
            break;

        case Stopped:
        case default:
            break;
    }

    return;
}

void FightController::mouseMoved(MouseContext context) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->mousePressed(context);
            break;
            
        case Final:
            FinalMenu->mousePressed(context);
            break;

        case Paused:
            PauseMenu->mousePressed(context);
            break;

        case Stopped:
        case default:
            break;
    }
    return;
}

void FightController::keyPressed(KeyboardContext context) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->keyPressed(context);
            break;
            
        case Final:
            FinalMenu->keyPressed(context);
            break;

        case Paused:
            PauseMenu->keyPressed(context);
            break;

        case Stopped:
        case default:
            break;
    }

    return;
}

void FightController::keyReleased(KeyboardContext context) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->keyReleased(context);
            break;
            
        case Final:
            FinalMenu->keyReleased(context);
            break;

        case Paused:
            PauseMenu->keyReleased(context);
            break;

        case Stopped:
        case default:
            break;
    }

    return;
}

void FightController::timeEvent(Time_t dt) {
    switch (currentStatus) {
        
        case Fighting:
            currentFight->timeEvent(dt);
            break;
            
        case Final:
            FinalMenu->timeEvent(dt);
            break;

        case Paused:
            PauseMenu->timeEvent(dt);
            break;

        case Stopped:
        case default:
            break;
    }

    return;
}