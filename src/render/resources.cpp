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

const sf::Texture* Resources::findTexture(const std::string& RelativePath, bool Smooth) {
    sf::Texture* Found = nullptr;
    if (auto It = Textures.find(RelativePath); It != Textures.end()) {
        Found = It->second.get();
    } else if (!MissingTextures.contains(RelativePath)) {
        auto Tex = std::make_unique<sf::Texture>();
        // Checked first so that SFML does not print its own error for a file
        // that is simply not there yet.
        const auto Path = Root / RelativePath;
        if (!std::filesystem::is_regular_file(Path) || !Tex->loadFromFile(Path)) {
            MissingTextures.insert(RelativePath);
            return nullptr;
        }
        log::debug("texture {} ({}x{})", RelativePath, Tex->getSize().x, Tex->getSize().y);
        Found = Textures.emplace(RelativePath, std::move(Tex)).first->second.get();
    }
    if (Found) Found->setSmooth(Smooth);
    return Found;
}

void Resources::clearTextures() {
    Textures.clear();
    MissingTextures.clear();
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
