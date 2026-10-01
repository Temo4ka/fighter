#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

// Кэш текстур и шрифтов (замена старого SpriteManager).
// Владеет ресурсами через unique_ptr: адреса не меняются при добавлении новых,
// поэтому ссылки, выданные раньше, остаются действительными.
namespace fighter::render {

class Resources {
public:
    explicit Resources(std::filesystem::path root);

    // Пути относительно корня проекта: "assets/fonts/JetBrainsMono-Regular.ttf".
    // При ошибке бросают std::runtime_error с путём к файлу.
    const sf::Texture& texture(const std::string& relativePath);
    const sf::Font& font(const std::string& relativePath);

    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> textures_;
    std::unordered_map<std::string, std::unique_ptr<sf::Font>> fonts_;
};

} // namespace fighter::render
