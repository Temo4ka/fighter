#include "../include/spriteManager.hpp"

void SpriteManager::loadTexture(uint64_t id, const std::string& filename) {
    if (textures.find(id) != textures.end()) {
        throw std::runtime_error("ID in textures already exist: " + std::to_string(id));
    }
 
    try {
        textures[id] = sf::Texture(std::filesystem::path(filename));
    } catch (const sf::Exception) {
        throw std::runtime_error("texture not found in file: " + filename);
    }
}

void setScreenSize(unsigned int screen_w_, unsigned int screen_h_) {
    screen_w = screen_w_;
    screen_h = screen_h_;
}

void SpriteManager::reset() {
    textures.clear();
}

size_t getSize() const {
    return textures.size();
}

sf::Texture getTexture(uint64_t id) {
    if (textures.find(id) == textures.end()) {
        throw std::runtime_error("texture not found by ID: " + id);
    }
    return textures[id];
}