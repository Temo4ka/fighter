#pragma once

#include <string>
#include <cstdint>

#include "vec2.hpp"
#include "geometry.hpp"
#include "spriteManager.hpp"

struct InfoBase {

    virtual void draw(sf::RenderWindow &screen);
}

struct SpriteInfo : public InfoBase {
    uint64_t textureID;
    Vec2 spritePos;
    Vec2 spriteSize;
    Vec2 frame;

    explicit SpriteInfo(uint64_t &textureID_, Vec2 &spritePos_, Vec2 &spriteSize_):
    textureID (textureID_),
    spritePos (spritePos_),
    spriteSize (spriteSize_),
    frame (Vec2(0, 0))
    {}
};

class Drawable {
  public:

    explicit Drawable(SpriteInfo &info, Vec2 &pos_, Vec2 &size_):
    visible (true),
    spriteInfo (info),
    pos (pos_),
    size (size_)
    {}

    Vec2 getPos()  const { return pos;  }
    Vec2 getSize() const { return size; }
    Rect getRect() const { return Rect(pos.x, pos.y, size.x, size.y); }

    void setPos  (Vec2      pos_) { pos  = pos_;  }
    void setSize (Vec2     size_) { size = size_; }

    void move (Vec2 delta) { pos += delta; }

    SpriteInfo  getSpriteInfo() const { return spriteInfo; }
    SpriteInfo& getSpriteInfo()       { return spriteInfo; }
    
    void setSpriteInfo(const SpriteInfo& info) {spriteInfo = info;}
    void setFrame(Vec2 frame_pos) { spriteInfo.frame = frame_pos; }
    void setTextureID(uint64_t id) { spriteInfo.textureID = id; }
    void setVisible(bool vis) {visible = vis;}
    bool getVisible() const {return visible;}

  private:
    bool visible;
    SpriteInfo spriteInfo;
    Vec2 pos;
    Vec2 size;
};

class GraphicsModule {
  public:

    explicit GraphicsModule() = default;

    void draw();

    void insertObject(Drawable *obj) { drawingQueue.push_back(obj); }

    void insertObject(Drawable *obj, size_t ind) { 
        size_t position = std::min(drawingQueue.size(), ind);
        drawingQueue.insert(drawingQueue.begin() + position, obj);
    }

    void eraseObject(Drawable *obj) { 
        for (auto iter = drawingQueue.begin(); iter != drawingQueue.end(); iter++)
            if (*iter == obj) {
                drawingQueue.erase(iter);
            }
    }

    void eraseObject(size_t ind) {
        size_t position = std::min(drawingQueue.size(), ind);
        drawingQueue.erase(drawingQueue.begin() + position);
    }

 private:
    std::vector<Drawable*> drawingQueue;

    sf::RenderTexture screen;
    SpriteManager& sprite_man;
    sf::Font fnt;
};