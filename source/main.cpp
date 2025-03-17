#include "../include/fight_controller.hpp"
#include "../include/DSL.hpp"

int main() {
    MESSAGE_CLEAR();

    std::vector<std::string> textureList = { BACKGROUND_FILE, BASE_OBJECT_FILE };

    GraphicsModule graphics(textureList);
    PhysicsModule  physics;
    EventManager event_manager;

    SpriteManager &sprite_manager = graphics.getSpriteManager();

    FighterInfo player1(sprite_manager, sprite_manager.loadTexture("./assets/players/player1.jpg"), 10, 1, 1);
    FighterInfo player2(sprite_manager, sprite_manager.loadTexture("./assets/players/player2.jpg"), 1, 10, 1);
    FightController fight_controller(physics, graphics, player1, player2);

    event_manager.enable();

    event_manager.CreateClockHandler(physics, &PhysicsModule::updateObjects);
    event_manager.CreateClockHandler(graphics, &GraphicsModule::TimeEvent);
    event_manager.CreateClockHandler(fight_controller, &FightController::timeEvent);

    event_manager.CreateKeyPressHandler(fight_controller, &FightController::keyPressed);
    event_manager.CreateKeyReleaseHandler(fight_controller, &FightController::keyReleased);
    event_manager.CreateMouseMoveHandler(fight_controller, &FightController::mouseMoved);
    event_manager.CreateMousePressHandler(fight_controller, &FightController::mousePressed);
    event_manager.CreateMouseReleaseHandler(fight_controller, &FightController::mouseReleased);

    Vec2 mousePosition(0, 0);

    sf::Clock clk = {};
    uint64_t last_time = 0;

    while (graphics.isWindowOpen()) {
        MSG("BLYAT");
        while (auto event = graphics.windowPollEvent()) {
            MSG("HUI");
            if (event->is<sf::Event::KeyPressed>()) {
                auto sf_context = event->getIf<sf::Event::KeyPressed>();
                event_manager.keyPress({sf_context->alt, sf_context->shift, sf_context->control, (Key) sf_context->code});
            }

            if (event->is<sf::Event::KeyReleased>()) {
                auto sf_context = event->getIf<sf::Event::KeyReleased>();
                event_manager.keyRelease({sf_context->alt, sf_context->shift, sf_context->control, (Key) sf_context->code});
            }

            if (event->is<sf::Event::MouseMoved>()) {
                auto sf_context = event->getIf<sf::Event::MouseMoved>();

                mousePosition.x = sf_context->position.x;
                mousePosition.y = sf_context->position.y;

                event_manager.mouseMove({mousePosition, MouseButton::Unknown});
            }

            if (event->is<sf::Event::MouseButtonPressed>()) {
                auto sf_context = event->getIf<sf::Event::MouseButtonPressed>();

                MouseButton buttonPressed = getMouseButton((int) sf_context->button);

                event_manager.mousePress({mousePosition, buttonPressed});
            }

            if (event->is<sf::Event::MouseButtonReleased>()) {
                auto sf_context = event->getIf<sf::Event::MouseButtonReleased>();

                MouseButton buttonPressed = getMouseButton((int) sf_context->button);

                event_manager.mouseRelease({mousePosition, buttonPressed});
            }

            if (event->is<sf::Event::Closed>())
                graphics.close();
        }

        uint64_t current_time = clk.getElapsedTime().asMicroseconds();
        if (current_time - last_time >= 10000) {
            Time_t dt = (double)(current_time - last_time) / 1000000;
            event_manager.Clock(dt);
            last_time = current_time;
            MESSAGE_CLEAR();
        }
    }
}

MouseButton getMouseButton(int code) {
    switch (code) {
        case (int) MouseButton::Left: 
            return MouseButton::Left;
        case (int) MouseButton::Right:
            return MouseButton::Right;

        default:
            return MouseButton::Unknown;
    }
}