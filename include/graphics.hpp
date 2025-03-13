#pragma once

#include <string>
#include <cstdint>

#include "vec2.hpp"
#include "geometry.hpp"
#include "sprite_manager.hpp"

static class Drawable {  
  public:
    Drawable() : visible(false), height(0), width(0), posX(0), posY(0), scale(1) {}

    Drawable(int height, int width, int posX, int posY, int scale) {
        this->visible = true;
        this->height  = height;
        this->width   = width;
        this->posX    = posX;
        this->posY    = posY;
        this->scale   = scale;
    }

    virtual void draw(sf::RenderWindow &window) = 0;

    void setVisible(bool vis) {visible = vis;}
    bool getVisible() const {return visible;}
    
    void setheight(int height) { this->height = height; }
    int getheight() const { return height; }

    void setwidth(int width) { this->width = width; }
    int getwidth() const { return width; }

    void setPosX(int posX) { this->posX = posX; }
    int getPosX() const { return posX; }

    void setPosY(int posY) { this->posY = posY; }
    int getPosY() const { return posY; }

    void setScale(int scale) { this->scale = scale; }
    int getScale() const { return scale; }

    void changePosition(int posX, int posY) {
        this->posX = posX;
        this->posY = posY;
    }

    void resize(int sizeX, int width) {
        this->height = height;
        this->width  = width;
    }

  private:
    bool visible; // can be public
    int height, width; // мб нужен другой тип
    int posX, posY; // мб нужен другой тип
    int scale; // мб нужен другой тип
};


class DrawableSprite : public Drawable {
  public:
    // не уверен надо ли передавать текстуру по ссылке
    explicit DrawableSprite(sf::Texture texture) {
        sprite = texture;
    }

    // не уверен надо ли передавать текстуру по ссылке
    DrawableSprite(sf::Textrure texture, int height, int width, int posX, int posY, int scale): 
        Drawable(height, width, posX, posY, scale)  
    {
        sprite = texture;
    }

    sf::Sprite getSprite() const {return sprite;}

	void draw(sf::RenderWindow &window) { window.draw(sprite); }
  private: 
    sf::Sprite sprite;
};

class DrawableText : public Drawable {
  public:
    // не уверен надо ли передавать шрифт по ссылке
    DrawableText(const sf::Font &font) {
        text = font;
    }

    // не уверен надо ли передавать шрифт по ссылке
    DrawableSprite(const sf::Font &font, int height, int width, int posX, int posY, int scale): 
        Drawable(height, width, posX, posY, scale)  
    {
        text = font;
    }

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