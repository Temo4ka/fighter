#include "../include/sprite_manager.hpp"
#include "../include/DSL.hpp"

uint64_t hash(const std::string& string) {
    const uint64_t FNV_PRIME = 1099511628211ULL; // Простое число для FNV-64
    const uint64_t OFFSET_BASIS = 14695981039346656037ULL; // Начальное значение

    uint64_t hsh = OFFSET_BASIS;

    for (char chr : string) {
        hsh ^= static_cast<uint8_t>(chr); // XOR с каждым байтом символа
        hsh *= FNV_PRIME;              // Умножение на простое число
    }

    return hsh;
}

SpriteManager::SpriteManager(std::vector<std::string> &loadList) {
    for (auto cur_string : loadList)
        loadTexture(cur_string);
    
    return;
}

uint64_t SpriteManager::loadTexture(const std::string& filename) {
    uint64_t id = hash(filename);

    if (textures.find(id) != textures.end())
        return id;
 
    try {
        textures[id] = sf::Texture(std::filesystem::path(filename));
        MESSAGE("%s size: (%u, %u)", filename.c_str(), textures[id].getSize().x, textures[id].getSize().y);
    } catch (const sf::Exception) {
        MSG("HUI");

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

sf::Texture *SpriteManager::getTexture(uint64_t id) {
    if (textures.find(id) == textures.end()) {
        throw std::runtime_error("texture not found by ID: " + std::to_string(id));
    }

    MESSAGE("(%p) TEXTURE BLYAD: (%u, %u)", this, textures[id].getSize().x, textures[id].getSize().y);

    return &textures[id];
}