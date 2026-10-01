#include "render/resources.hpp"

#include <stdexcept>

#include "core/log.hpp"

namespace fighter::render {

Resources::Resources(std::filesystem::path root) : root_(std::move(root)) {}

const sf::Texture& Resources::texture(const std::string& relativePath) {
    if (auto it = textures_.find(relativePath); it != textures_.end()) return *it->second;

    const auto path = root_ / relativePath;
    auto tex = std::make_unique<sf::Texture>();
    if (!tex->loadFromFile(path)) {
        throw std::runtime_error("не удалось загрузить текстуру: " + path.string());
    }
    tex->setSmooth(true);
    log::debug("texture {} ({}x{})", relativePath, tex->getSize().x, tex->getSize().y);
    return *textures_.emplace(relativePath, std::move(tex)).first->second;
}

const sf::Font& Resources::font(const std::string& relativePath) {
    if (auto it = fonts_.find(relativePath); it != fonts_.end()) return *it->second;

    const auto path = root_ / relativePath;
    auto fnt = std::make_unique<sf::Font>();
    if (!fnt->openFromFile(path)) {
        throw std::runtime_error("не удалось загрузить шрифт: " + path.string());
    }
    log::debug("font {}", relativePath);
    return *fonts_.emplace(relativePath, std::move(fnt)).first->second;
}

} // namespace fighter::render
