#include "../include/spriteManager.hpp"

const HASH_CONST = 53;

uint64_t hash(const std::string &string) {
    uint64_t hsh = 0;

    for (auto chr : string) {
        hsh = hsh * HASH_CONST + chr;
    }

    return hsh;
}

uint64_t SpriteManager::loadTexture(const std::string& filename) {
    uint64_t id = hash(filename);

    if (textures.find(id) != textures.end()) return id;
 
    try {
        textures[id] = sf::Texture(std::filesystem::path(filename));
    } catch (const sf::Exception) {
        throw std::runtime_error("texture not found in file: " + filename);
    }

    return id;
}

void SpriteManager::setScreenSize(unsigned int screen_w_, unsigned int screen_h_) {
    screen_w = screen_w_;
    screen_h = screen_h_;
}

void SpriteManager::reset() {
    textures.clear();
}

size_t SpriteManager::getSize() const {
    return textures.size();
}

sf::Texture SpriteManager::getTexture(uint64_t id) {
    if (textures.find(id) == textures.end()) {
        throw std::runtime_error("texture not found by ID: " + id);
    }
    return textures[id];
}