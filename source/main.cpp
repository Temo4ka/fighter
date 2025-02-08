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

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::KeyPressed>) {
                auto sf_context = event->KeyPressed;
                KeyboardContext context;

                context.key = sf_context.key;

                context.alt = sf_context.alt;
                context.shift = sf_context.shift;
                context.ctrl = sf_context.ctrl;

                event_manager.keyPress(context);
            }

            if (event->is<sf::Event::KeyReleased>) {
                auto sf_context = event->KeyReleased;
                KeyboardContext context;

                context.key = sf_context.code;

                context.alt = sf_context.alt;
                context.shift = sf_context.shift;
                context.ctrl = sf_context.ctrl;

                event_manager.keyRelease(context);
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

        window.clear();
        window.draw(shape);
        window.display();
    }
}