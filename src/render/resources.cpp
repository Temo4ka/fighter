#include "render/resources.hpp"

#include <stdexcept>

#include "core/log.hpp"

namespace fighter::render {

Resources::Resources(std::filesystem::path RootPath) : Root(std::move(RootPath)) {}

const sf::Texture& Resources::getTexture(const std::string& RelativePath) {
    if (auto It = Textures.find(RelativePath); It != Textures.end()) return *It->second;

    const auto Path = Root / RelativePath;
    auto Tex = std::make_unique<sf::Texture>();
    if (!Tex->loadFromFile(Path)) {
        throw std::runtime_error("cannot load texture: " + Path.string());
    }
    Tex->setSmooth(true);
    log::debug("texture {} ({}x{})", RelativePath, Tex->getSize().x, Tex->getSize().y);
    return *Textures.emplace(RelativePath, std::move(Tex)).first->second;
}

const sf::Font& Resources::getFont(const std::string& RelativePath) {
    if (auto It = Fonts.find(RelativePath); It != Fonts.end()) return *It->second;

    const auto Path = Root / RelativePath;
    auto Font = std::make_unique<sf::Font>();
    if (!Font->openFromFile(Path)) {
        throw std::runtime_error("cannot load font: " + Path.string());
    }
    log::debug("font {}", RelativePath);
    return *Fonts.emplace(RelativePath, std::move(Font)).first->second;
}

} // namespace fighter::render
