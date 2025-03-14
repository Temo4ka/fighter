#pragma once

#include <SFML/Graphics.hpp>
#include <cstdint>
#include <unordered_map>

class SpriteManager {
  public:

    explicit SpriteManager() = default;

    // loads texture from file and inserts it into map
    uint64_t loadTexture(const std::string& filename); 

    // resets all loaded sprites
    void reset();

    // sets new screen size to correct scale of sprites
    void setScreenSize(unsigned int screen_w_, unsigned int screen_h_);

    // sf::Sprite getSprite(const Drawable& obj, const Camera& cam);

    // gets a rectangular shape with help of SpriteInfo object (not needed)
    //sf::Sprite getSprite(const Rect& rect, const SpriteInfo& sprite_info);

    // gets number of loaded Sprites
    size_t getSize() const; 

    sf::Texture getTexture(uint64_t id) const;

  private:
    std::unordered_map<uint64_t, sf::Texture> textures;
    unsigned int screen_w;
    unsigned int screen_h;
};

uint64_t hash(const std::string &string);
