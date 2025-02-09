#pragma once

#include <string>
#include <cstdint>

#include "vec2.hpp"
#include "geometry.hpp"
#include "spriteManager.hpp"

class Drawable {  
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
    explicit DrawableSprite(const uint64_t textureID): sprite(SpriteManager::getTexture(textureID)) {}

    sf::Sprite getSprite() const {return sprite;}

    uint64_t getTextureID() const {return textureID;}
    
    void draw(sf::RenderWindow &window) {window.draw(sprite);}
  private: 
    sf::Sprite sprite;
    uint64_t textureID;
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

 private:
    sf::RenderWindow window(sf::VideoMode({WINDOW_WID, WINDOW_HGT}), "RPG FIGHTER");
    sf::RenderTexture screen(sf::Vector2u(WINDOW_WID, WINDOW_HGT));
    sf::Font fnt;

    std::vector<Drawable*> drawingQueue;
    
    SpriteManager sprite_man;
};