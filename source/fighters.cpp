#include "../include/fighters.hpp"
#include "../include/config.hpp"

void Fighter::mousePressed(MouseContext context) {
    switch (fighterStartParams.getCommand(context.button)) {

        case FighterInfo::Commands::SIT:
            sit();
            break;
        
        case FighterInfo::Commands::JUMP:
            jump();
            break;
        
        case FighterInfo::Commands::MOVE_LEFT:
            move_left();
            break;
        
        case FighterInfo::Commands::MOVE_RIGHT:
            move_right();
            break;
        
        case FighterInfo::Commands::ATTACK_WITH_HAND:
            attack_with_hand();
            break;

        case FighterInfo::Commands::ATTACK_WITH_LEG:
            attack_with_leg();
            break;

        case FighterInfo::Commands::UNDEFINED:
        default:
            break;
    }

    return;
}

void Fighter::mouseReleased(MouseContext context) {
    return;
}

void Fighter::mouseMoved(MouseContext context) {
    return;
}

void Fighter::keyPressed(KeyboardContext context) {
    switch (fighterStartParams.getCommand(context.key)) {

        case FighterInfo::Commands::SIT:
            sit();
            break;
        
        case FighterInfo::Commands::JUMP:
            jump();
            break;
        
        case FighterInfo::Commands::MOVE_LEFT:
            move_left();
            break;
        
        case FighterInfo::Commands::MOVE_RIGHT:
            move_right();
            break;
        
        case FighterInfo::Commands::ATTACK_WITH_HAND:
            attack_with_hand();
            break;

        case FighterInfo::Commands::ATTACK_WITH_LEG:
            attack_with_leg();
            break;

        case FighterInfo::Commands::UNDEFINED:
        default:
            break;
    }

    return;
}

void Fighter::keyReleased(KeyboardContext context) {
    return;
}

void Fighter::timeEvent(Time_t dt) {
    return;
}

void Fighter::sit() {

    return;
}

void Fighter::jump() {
    if (getState() != State::IN_AIR)
        velocity += Vec2(0, BASE_JUMP_VEL);
}

void Fighter::move_right() {
    if (velocity.x + BASE_ACCEL <= RUN_SPEED_LIMIT)
        velocity.x += BASE_ACCEL;
}

void Fighter::move_left() {
    if (velocity.x - BASE_ACCEL >= -RUN_SPEED_LIMIT)
        velocity.x -= BASE_ACCEL;
}

void Fighter::attack_with_hand() {

}

void Fighter::attack_with_leg() {
    
}
