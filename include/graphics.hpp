#pragma once

#include <string>
#include <cstdint>

#include "vec2.hpp"
#include "geometry.hpp"
#include "sprite_manager.hpp"

class Drawable {  
  public:
    Drawable() : visible(false) {}

    virtual void draw(sf::RenderWindow &window) = 0;

    void setVisible(bool vis) {visible = vis;}
    bool getVisible() const {return visible;}

    virtual void setPosition(const sf::Vector2f& position) = 0;
    virtual sf::Vector2f getPosition() const = 0;

    virtual void setScale(const sf::Vector2f& scale) = 0;
    virtual sf::Vector2f getScale() const = 0;

  private:
    bool visible; // can be public
};


class DrawableSprite : public Drawable {
  public:
    DrawableSprite(sf::Textrure &texture) { sprite = texture; }

    DrawableSprite(sf::Textrure &texture, const sf::Vector2f position_, const sf::Vector2f size_, const sf::Vector2f scale_)
    { 
      sprite = sf::Sprite(texture);
      sprite.setPosition(position);
      sprite.setScale(scale);
      sprite.setSize(size);
    }

    sf::Sprite getSprite() const { return sprite; }

    void setPosition(const sf::Vector2f& position) override { sprite.setPosition(position); }
    sf::Vector2f getPosition() const override { return sprite.getPosition(); }

    void setSize(const sf::Vector2f& size) override { sprite.setSize(size); }
    sf::Vector2f getSize() const override { return sprite.getSize(); }

    void setScale(const sf::Vector2f& scale) override { sprite.setScale(scale); }
    sf::Vector2f getScale() const override { return sprite.getScale(); }

	void draw(sf::RenderWindow &window) override { window.draw(sprite); }

  private: 
    sf::Sprite sprite;
};

class DrawableText : public Drawable {
  public:
    DrawableText(const sf::Font &font) { text = font; }

    sf::Text getText() const { return text; }

    void setPosition(const sf::Vector2f& position) override { text.setPosition(position); }
    sf::Vector2f getPosition() const override { return text.getPosition(); }

    void setScale(const sf::Vector2f& scale) override { text.setScale(scale); }
    sf::Vector2f getScale() const override { return text.getScale(); }

    // нейронка предложила, вроде полезно
    void setTextString(const std::string& str) { text.setString(str); }
    std::string getTextString() const { return text.getString().toAnsiString(); }

    void setTextSize(unsigned int size) { text.setCharacterSize(size); }

    unsigned int getTextSize() const { return text.getCharacterSize(); }

    void draw(sf::RenderWindow &window) override { window.draw(text); }
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