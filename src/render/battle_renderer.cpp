#include "render/battle_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <vector>

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include "render/assets.hpp"

namespace fighter::render {
namespace {

const std::array<sf::Color, 2> FighterColors = {sf::Color(200, 70, 60), sf::Color(60, 110, 200)};

/// A rectangle in world coordinates given by its bottom-left corner and size, m.
sf::RectangleShape makeWorldRect(Vec2 BottomLeft, Vec2 Size) {
    sf::RectangleShape Rect({Size.X, Size.Y});
    // Y is flipped in the world view: the top of the rectangle is BottomLeft.Y + Size.Y.
    Rect.setPosition(Camera::toDraw({BottomLeft.X, BottomLeft.Y + Size.Y}));
    return Rect;
}

/// Parts of the far side (the "R" limbs of a fighter seen from its left) are
/// drawn first and darker, so the near limbs stay on top.
bool isFarSide(BodyPart Part) {
    switch (Part) {
        case BodyPart::UpperArmR:
        case BodyPart::ForearmR:
        case BodyPart::ThighR:
        case BodyPart::ShinR:
        case BodyPart::FootR:
            return true;
        default:
            return false;
    }
}

void drawPart(sf::RenderTarget& Target, const PartTransform& Part, sf::Color Color) {
    sf::RectangleShape Box({Part.Size.X, Part.Size.Y});
    Box.setOrigin({Part.Size.X * 0.5f, Part.Size.Y * 0.5f});
    Box.setPosition(Camera::toDraw(Part.Position));
    // Y is flipped in the world view, so counter-clockwise becomes clockwise.
    Box.setRotation(sf::radians(-Part.Angle));
    Box.setFillColor(Color);
    Box.setOutlineColor(sf::Color::Black);
    Box.setOutlineThickness(-0.015f);
    Target.draw(Box);
}

void drawParts(sf::RenderTarget& Target, const std::vector<PartTransform>& Parts, sf::Color Color) {
    const sf::Color Far(static_cast<uint8_t>(Color.r * 3 / 5), static_cast<uint8_t>(Color.g * 3 / 5),
                        static_cast<uint8_t>(Color.b * 3 / 5));
    for (const auto& Part : Parts) {
        if (isFarSide(Part.Part)) drawPart(Target, Part, Far);
    }
    for (const auto& Part : Parts) {
        if (!isFarSide(Part.Part)) drawPart(Target, Part, Color);
    }
}

} // namespace

BattleRenderer::BattleRenderer(Resources& Res) : Assets(Res) {}

void BattleRenderer::drawWorld(sf::RenderTarget& Target, const Camera& Cam,
                               const combat::RenderSnapshot& Snapshot) {
    Target.setView(Cam.getWorldView());

    // Background: scaled with its aspect ratio kept so that it covers the whole view.
    const sf::Texture& Bg = Assets.getTexture(assets::BackgroundPath);
    const Vec2 View = Cam.getViewSizeM();
    const Vec2 TexSize{static_cast<float>(Bg.getSize().x), static_cast<float>(Bg.getSize().y)};
    const float Scale = std::max(View.X / TexSize.X, View.Y / TexSize.Y);
    sf::Sprite Background(Bg);
    Background.setOrigin({TexSize.X * 0.5f, TexSize.Y * 0.5f});
    Background.setScale({Scale, Scale});
    Background.setPosition(Camera::toDraw(Cam.getCenterM()));
    Target.draw(Background);

    // Floor: a band below y = 0 across the whole view.
    const float FloorDepth = View.Y;
    sf::RectangleShape Floor =
        makeWorldRect({Cam.getCenterM().X - View.X, -FloorDepth}, {View.X * 2.0f, FloorDepth});
    Floor.setFillColor(sf::Color(45, 40, 38));
    Target.draw(Floor);

    // Placeholder fighters: a colored box per body part, or one rectangle
    // when there are no parts.
    for (auto&& [Fighter, Color] : std::views::zip(Snapshot.Fighters, FighterColors)) {
        if (!Fighter.Parts.empty()) {
            drawParts(Target, Fighter.Parts, Color);
            continue;
        }
        sf::RectangleShape Body =
            makeWorldRect({Fighter.Position.X - Fighter.Size.X * 0.5f, Fighter.Position.Y}, Fighter.Size);
        Body.setFillColor(Color);
        Body.setOutlineColor(sf::Color::Black);
        Body.setOutlineThickness(-0.02f);
        Target.draw(Body);

        // The "eye" shows which way the fighter is facing.
        const float EyeX = Fighter.Position.X + (Fighter.FacingRight ? 0.12f : -0.12f) - 0.04f;
        sf::RectangleShape Eye = makeWorldRect({EyeX, Fighter.Position.Y + Fighter.Size.Y - 0.3f}, {0.08f, 0.08f});
        Eye.setFillColor(sf::Color::White);
        Target.draw(Eye);
    }
}

void BattleRenderer::drawHud(sf::RenderTarget& Target, const Camera& Cam,
                             const combat::RenderSnapshot& Snapshot) {
    Target.setView(Cam.getScreenView());
    const float Width = static_cast<float>(Cam.getWindowSize().x);
    const sf::Font& Font = Assets.getFont(assets::MonoFontPath);

    // HP bars: the left fighter on the left, the right one on the right.
    constexpr float BarW = 360.0f, BarH = 18.0f, Margin = 24.0f;
    for (size_t Index = 0; Index < Snapshot.Fighters.size(); ++Index) {
        const combat::FighterView& Fighter = Snapshot.Fighters[Index];
        const float Ratio = Fighter.MaxHp > 0.0f ? std::clamp(Fighter.Hp / Fighter.MaxHp, 0.0f, 1.0f) : 0.0f;
        const float BarX = (Index == 0) ? Margin : Width - Margin - BarW;

        sf::RectangleShape Back({BarW, BarH});
        Back.setPosition({BarX, Margin});
        Back.setFillColor(sf::Color(0, 0, 0, 160));
        Back.setOutlineColor(sf::Color(230, 230, 230));
        Back.setOutlineThickness(1.0f);
        Target.draw(Back);

        sf::RectangleShape Fill({BarW * Ratio, BarH});
        // The right bar shrinks towards the screen center, as in classic fighting games.
        Fill.setPosition({Index == 0 ? BarX : BarX + BarW * (1.0f - Ratio), Margin});
        Fill.setFillColor(FighterColors[Index]);
        Target.draw(Fill);
    }

    sf::Text Timer(Font, std::format("{:.0f}", std::ceil(Snapshot.TimeLeftSec)), 28);
    const sf::FloatRect Bounds = Timer.getLocalBounds();
    Timer.setPosition({(Width - Bounds.size.x) * 0.5f - Bounds.position.x, Margin - 6.0f});
    Timer.setFillColor(sf::Color::White);
    Timer.setOutlineColor(sf::Color::Black);
    Timer.setOutlineThickness(2.0f);
    Target.draw(Timer);
}

} // namespace fighter::render
