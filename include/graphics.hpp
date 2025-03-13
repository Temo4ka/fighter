#pragma once

#include <string>
#include <cstdint>

#include "vec2.hpp"
#include "geometry.hpp"
#include "sprite_manager.hpp"

static class Drawable {  
  public:
    Drawable() : visible (true) {}

    virtual void draw(sf::RenderWindow &window) = 0;

    void setVisible(bool vis) {visible = vis;}
    bool getVisible() const {return visible;}

  private:
    bool visible; // can be public
};


class DrawableSprite : public Drawable {
  public:
    explicit DrawableSprite(sf::Texture texture):
		sprite(texture)
	{}

    sf::Sprite getSprite() const {return sprite;}

	void draw(sf::RenderWindow &window) { window.draw(sprite); }
  private: 
    sf::Sprite sprite;
};

class DrawableText : public Drawable {
  public:
    DrawableText(const sf::Font &font) : text(font) {}

    sf::Text getText() const {return text;}

    void draw(sf::RenderWindow &window) {window.draw(text);}
  private:
    sf::Text text;
};

class GraphicsModule {
  public:

    explicit GraphicsModule() = default;

    void draw();

    void insertObject(Drawable *obj) { drawingQueue.push_back(obj); }

    void insertObject(Drawable *obj, size_t ind);

    void eraseObject(Drawable *obj);

    void eraseObject(size_t ind);

    void TimeEvent(Time_t dt);

	SpriteManager &getSpriteManager() { return sprite_man; }

	std::optional<sf::Event> windowPollEvent() { return window.pollEvent(); }

	bool isWindowOpen() { return window.isOpen(); }

	void close() { window.close(); }

 private:
    sf::RenderWindow  window = sf::RenderWindow(sf::VideoMode({WINDOW_WID, WINDOW_HGT}), "RPG FIGHTER");
    sf::RenderTexture screen = sf::RenderTexture(sf::Vector2u(WINDOW_WID, WINDOW_HGT));
    sf::Font fnt;

    std::vector<Drawable*> drawingQueue;
    
    SpriteManager sprite_man;
};