//===- render/resources.hpp - Texture and font cache ------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares render::Resources, the cache of textures and fonts.
///
/// Resources are owned through unique_ptr, so their addresses do not change
/// when new ones are added and references handed out earlier stay valid.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

namespace fighter::render {

class Resources {
public:
    explicit Resources(std::filesystem::path RootPath);

    /// Paths are relative to the project root, for example
    /// "assets/fonts/JetBrainsMono-Regular.ttf". Throws std::runtime_error
    /// with the file path on failure.
    const sf::Texture& getTexture(const std::string& RelativePath);
    const sf::Font& getFont(const std::string& RelativePath);

    /// Like getTexture(), but returns nullptr instead of throwing when the
    /// file cannot be loaded; the failure is remembered, so a missing file
    /// is tried once until clearTextures(). \p Smooth sets linear filtering.
    const sf::Texture* findTexture(const std::string& RelativePath, bool Smooth = true);
    /// Forgets all textures, so the next request reads the file again (F5).
    /// References and pointers handed out earlier become invalid.
    void clearTextures();

    const std::filesystem::path& getRoot() const { return Root; }

private:
    std::filesystem::path Root;
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> Textures;
    std::unordered_set<std::string> MissingTextures;
    std::unordered_map<std::string, std::unique_ptr<sf::Font>> Fonts;
};

} // namespace fighter::render
