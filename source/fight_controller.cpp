#include "../include/fight_controller.hpp"

void FightController::startFight(const FighterInfo &fighter1, const FighterInfo &fighter2) {
    currentFight = Fight(graphModule, fighter1, fighter2);

    auto objects = currentFight.getObjects();
    for (auto cur_object : objects) {
        physModule.addObject(cur_object);
        graphModule.insertObject(cur_object);
    }

    physModule.addObject(currentFight.getLftFighter());
    physModule.addObject(currentFight.getRgtFighter());

    graphModule.insertObject(currentFight.getLftFighter());
    graphModule.insertObject(currentFight.getRgtFighter());

    return;
}

void FightController::restartFight() {
    currentFight.restart();

    return;
}

void FightController::stopFight() {
    physModule.eraseObject(currentFight.getRgtFighter());
    physModule.eraseObject(currentFight.getLftFighter());

    graphModule.eraseObject(currentFight.getRgtFighter());
    graphModule.eraseObject(currentFight.getLftFighter());

    std::vector<Entity>& obj = currentFight.getObjects();
    for (auto it = obj.begin(); it != obj.end(); it++) {
        physModule.eraseObject(&(*it));
        graphModule.eraseObject(&(*it));
    }

    return;
}

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