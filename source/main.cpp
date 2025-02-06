#include "../include/fight_controller.hpp"

int main()
{
    sf::RenderWindow window(sf::VideoMode({WINDOW_WID, WINDOW_HGT}), "RPG FIGHTER");
    sf::CircleShape shape(100.f);
    shape.setFillColor(sf::Color::Green);

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        window.clear();
        window.draw(shape);
        window.display();
    }
}