#include "render/battle_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include "render/assets.hpp"

namespace fighter::render {
namespace {

const sf::Color kFighterColors[2] = {sf::Color(200, 70, 60), sf::Color(60, 110, 200)};

// Прямоугольник в мировых координатах: левый нижний угол + размер, м.
sf::RectangleShape worldRect(Vec2 bottomLeft, Vec2 size) {
    sf::RectangleShape rect({size.x, size.y});
    // В worldView Y перевёрнут: верх прямоугольника — это bottomLeft.y + size.y.
    rect.setPosition(Camera::toDraw({bottomLeft.x, bottomLeft.y + size.y}));
    return rect;
}

} // namespace

BattleRenderer::BattleRenderer(Resources& resources) : resources_(resources) {}

void BattleRenderer::drawWorld(sf::RenderTarget& target, const Camera& camera,
                               const combat::RenderSnapshot& snapshot) {
    target.setView(camera.worldView());

    // Фон: растягиваем с сохранением пропорций так, чтобы он закрыл весь вид.
    const sf::Texture& bg = resources_.texture(assets::kBackground);
    const Vec2 view = camera.viewSizeM();
    const Vec2 texSize{static_cast<float>(bg.getSize().x), static_cast<float>(bg.getSize().y)};
    const float scale = std::max(view.x / texSize.x, view.y / texSize.y);
    sf::Sprite background(bg);
    background.setOrigin({texSize.x * 0.5f, texSize.y * 0.5f});
    background.setScale({scale, scale});
    background.setPosition(Camera::toDraw(camera.centerM()));
    target.draw(background);

    // Пол: полоса ниже y = 0 на всю ширину вида.
    const float floorDepth = view.y;
    sf::RectangleShape floor = worldRect({camera.centerM().x - view.x, -floorDepth}, {view.x * 2.0f, floorDepth});
    floor.setFillColor(sf::Color(45, 40, 38));
    target.draw(floor);

    // Бойцы-плейсхолдеры.
    for (std::size_t i = 0; i < snapshot.fighters.size(); ++i) {
        const combat::FighterView& f = snapshot.fighters[i];
        sf::RectangleShape body = worldRect({f.position.x - f.size.x * 0.5f, f.position.y}, f.size);
        body.setFillColor(kFighterColors[i]);
        body.setOutlineColor(sf::Color::Black);
        body.setOutlineThickness(-0.02f);
        target.draw(body);

        // «Глаз» показывает, куда смотрит боец.
        const float eyeX = f.position.x + (f.facingRight ? 0.12f : -0.12f) - 0.04f;
        sf::RectangleShape eye = worldRect({eyeX, f.position.y + f.size.y - 0.3f}, {0.08f, 0.08f});
        eye.setFillColor(sf::Color::White);
        target.draw(eye);
    }
}

void BattleRenderer::drawHud(sf::RenderTarget& target, const Camera& camera,
                             const combat::RenderSnapshot& snapshot) {
    target.setView(camera.screenView());
    const float width = static_cast<float>(camera.windowSize().x);
    const sf::Font& font = resources_.font(assets::kMonoFont);

    // Полоски HP: левый боец слева, правый справа.
    constexpr float barW = 360.0f, barH = 18.0f, margin = 24.0f;
    for (std::size_t i = 0; i < snapshot.fighters.size(); ++i) {
        const combat::FighterView& f = snapshot.fighters[i];
        const float ratio = f.maxHp > 0.0f ? std::clamp(f.hp / f.maxHp, 0.0f, 1.0f) : 0.0f;
        const float x = (i == 0) ? margin : width - margin - barW;

        sf::RectangleShape back({barW, barH});
        back.setPosition({x, margin});
        back.setFillColor(sf::Color(0, 0, 0, 160));
        back.setOutlineColor(sf::Color(230, 230, 230));
        back.setOutlineThickness(1.0f);
        target.draw(back);

        sf::RectangleShape fill({barW * ratio, barH});
        // Правая полоска убывает к центру экрана, как в классических файтингах.
        fill.setPosition({i == 0 ? x : x + barW * (1.0f - ratio), margin});
        fill.setFillColor(kFighterColors[i]);
        target.draw(fill);
    }

    sf::Text timer(font, std::format("{:.0f}", std::ceil(snapshot.timeLeftSec)), 28);
    const sf::FloatRect bounds = timer.getLocalBounds();
    timer.setPosition({(width - bounds.size.x) * 0.5f - bounds.position.x, margin - 6.0f});
    timer.setFillColor(sf::Color::White);
    timer.setOutlineColor(sf::Color::Black);
    timer.setOutlineThickness(2.0f);
    target.draw(timer);
}

} // namespace fighter::render
