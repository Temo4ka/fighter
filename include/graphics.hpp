#pragma once

#include <string>
#include <cstdint>

#include "event.hpp"
#include "config.hpp"
#include "vec2.hpp"
#include "geometry.hpp"
#include "sprite_manager.hpp"
#include "DSL.hpp"

class Drawable {  
  public:
    Drawable() : visible(false) {}

    virtual void draw(sf::RenderTexture &window) = 0;

    void setVisible(bool vis) {visible = vis;}
    bool getVisible() const {return visible;}

    virtual void setPosition(const sf::Vector2f& position) = 0;
    virtual sf::Vector2f getPosition() const = 0;

    virtual void setScale(const sf::Vector2f& scale) = 0;
    virtual sf::Vector2f getScale() const = 0;
    
    virtual void setRotation(float rotation) = 0;
    virtual float getRotation() const = 0;

  private:
    bool visible; // can be public
};


class DrawableSprite : public Drawable {
  public:
    DrawableSprite(sf::Texture *texture):
      sprite (sf::Sprite(*texture))
    {}

    DrawableSprite(sf::Texture *texture, const sf::Vector2f &position_, const sf::Vector2f &scale_):
      sprite (sf::Sprite(*texture))
    { 
      MESSAGE("SPRITE CREATED HERE %p, SIZE :(%u, %u)", this, texture->getSize().x, texture->getSize().y);
      sprite.setPosition(position_);
      sprite.setScale(scale_);
    }

    sf::Sprite getSprite() const { return sprite; }

    void setPosition(const sf::Vector2f& position) override { sprite.setPosition(position); }
    sf::Vector2f getPosition() const override { return sprite.getPosition(); }

    void setScale(const sf::Vector2f& scale) override { sprite.setScale(scale); }
    sf::Vector2f getScale() const override { return sprite.getScale(); }
    
    void setRotation(float rotation) override { sprite.setRotation(sf::degrees(rotation)); }
    float getRotation() const override { return sprite.getRotation().asDegrees(); }

	  void draw(sf::RenderTexture &window) override { 
      MESSAGE("Sprite %p is drawn on position: (%lg, %lg)", this, sprite.getPosition().x, sprite.getPosition().y);

      window.draw(sprite); 
    }

  private: 
    sf::Sprite sprite;
};

class DrawableText : public Drawable {
  public:
    DrawableText(const sf::Font &font):
      text (font)
    {}

    sf::Text getText() const { return text; }

    void setPosition(const sf::Vector2f& position) override { text.setPosition(position); }
    sf::Vector2f getPosition() const override { return text.getPosition(); }

    void setScale(const sf::Vector2f& scale) override { text.setScale(scale); }
    sf::Vector2f getScale() const override { return text.getScale(); }
    
    void setRotation(float rotation) override { /* Текст не поддерживает поворот */ }
    float getRotation() const override { return 0.0f; }

    // нейронка предложила, вроде полезно
    void setTextString(const std::string& str) { text.setString(str); }
    std::string getTextString() const { return text.getString().toAnsiString(); }

    void setTextSize(unsigned int size) { text.setCharacterSize(size); }

    unsigned int getTextSize() const { return text.getCharacterSize(); }

    void draw(sf::RenderTexture &window) override { window.draw(text); }
  private:
    sf::Text text;
};

class GraphicsModule {
  public:

    explicit GraphicsModule() = default;

    explicit GraphicsModule(std::vector<std::string> &textureLoadList) :
      sprite_man(textureLoadList)
    {}

    void draw();

    void insertObject(Drawable *obj) { drawingQueue.push_back(obj); }

    void insertObject(Drawable *obj, size_t ind);

    void eraseObject(Drawable *obj);

    void eraseObject(size_t ind);

    void TimeEvent(Time_t dt);

	SpriteManager &getSpriteManager() { return sprite_man; }

	auto windowPollEvent() { return window.pollEvent(); }

	bool isWindowOpen() { return window.isOpen(); }

	void close() { window.close(); }

 private:
    sf::RenderWindow  window = sf::RenderWindow(sf::VideoMode({WINDOW_WID, WINDOW_HGT}), "RPG FIGHTER");
    sf::RenderTexture screen = sf::RenderTexture(sf::Vector2u(WINDOW_WID, WINDOW_HGT));
    sf::Font fnt;

    std::vector<Drawable*> drawingQueue;
    
    SpriteManager sprite_man;
};

#define DEBUG_

#ifdef DEBUG_

#include "hitbox.hpp"

class HitboxRect : public Drawable {
  public:
    HitboxRect(Hitbox &hitbox) {
        for (auto cur : hitbox.getRects()) {
            sf::Vector2f size = sf::Vector2f(cur.getSize().x, cur.getSize().y);
            sf::Vector2f pos  = sf::Vector2f(cur.getPos().x, cur.getPos().y);


            sf::RectangleShape newShape(size);
            newShape.setScale(scale);
            newShape.setPosition(pos);

            rects.push_back(newShape);
        }
    }

    sf::Vector2f getPosition() const override {return position; }
    void setPosition(const sf::Vector2f& newPosition) override {
        sf::Vector2f delta = newPosition - position;
        for (auto cur : rects) cur.setPosition(cur.getPosition() + delta);
    }

    sf::Vector2f getScale() const override { return scale; }
    void setScale(const sf::Vector2f& newScale) override {
        for (auto cur : rects) cur.setScale(newScale);
    }
    
    void setRotation(float rotation) override { /* Hitbox не поддерживает поворот */ }
    float getRotation() const override { return 0.0f; }

  private:
    sf::Vector2f position = {0, 0};
    sf::Vector2f scale = {1, 1};

    std::vector<sf::RectangleShape> rects;
};
#endif