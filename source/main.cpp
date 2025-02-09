#include "../include/fight_controller.hpp"

int main()
{
    GraphicsModule graphics;
    PhysicsModule  physics;
    EventManager event_manager;
    FightController fight_controller(physics, graphics);

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

    while (graphics.getWindow().isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::KeyPressed>) {
                auto sf_context = event->KeyPressed;
                event_manager.keyPress({sf_context.alt, sf_context.shift, sf_context.ctrl, sf_context.code});
            }

            if (event->is<sf::Event::KeyReleased>) {
                auto sf_context = event->KeyReleased;
                event_manager.keyRelease({sf_context.alt, sf_context.shift, sf_context.ctrl, sf_context.code});
            }

            if (event->is<sf::Event::MouseMoved>) {
                auto sf_context = event->MouseMoved;

                mousePosition.x = sf_context.position.x;
                mousePosition.y = sf_context.position.y;

                event_manager.mouseMove({mousePosition, MouseButton::Unknown});
            }

            if (event->is<sf::Event::MouseButtonPressed>) {
                auto sf_context = event->MouseButtonPressed;

                MouseButton buttonPressed = (sf_context.button == sf::Mouse::Button::Left ||
                                             sf_context.button == sf::Mouse::Button::Right  )? sf_context.button : MouseButton::Unknown;

                event_manager.mousePress({mousePosition, buttonPressed});
            }

            if (event->is<sf::Event::MouseButtonReleased>) {
                auto sf_context = event->MouseButtonReleased;

                MouseButton buttonPressed = (sf_context.button == sf::Mouse::Button::Left ||
                                             sf_context.button == sf::Mouse::Button::Right  )? sf_context.button : MouseButton::Unknown;

                event_manager.mouseRelease({mousePosition, buttonPressed});
            }

            if (event->is<sf::Event::Closed>())
                window.close();
        }

        uint64_t current_time = clk.getElapsedTime().asMicroseconds();
        if (current_time - last_time >= 10000) {
            Time_t dt = (double)(current_time - last_time) / 1000000;
            event_manager.Clock(dt);
            last_time = current_time;
        }
    }
}