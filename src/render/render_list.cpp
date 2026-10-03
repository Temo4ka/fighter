#include "render/render_list.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

namespace fighter::render {
namespace {

/// Points on each rounded end of a capsule.
constexpr size_t CapsuleArcPoints = 10;
/// The outline of a fallback capsule, m (drawn inwards).
constexpr float CapsuleOutlineM = 0.012f;

bool isScreenLayer(Layer Where) { return Where == Layer::Hud; }

/// Draws one primitive of a world layer (Screen = false) or the Hud layer.
class PrimitiveDrawer {
public:
    PrimitiveDrawer(sf::RenderTarget& Out, const Camera& View, const sf::Font& TextFont, bool InScreen)
        : Target(Out), Cam(View), Font(TextFont), Screen(InScreen) {}

    void operator()(const SpritePrim& Sprite) const;
    void operator()(const CapsulePrim& Capsule) const;
    void operator()(const RectPrim& Rect) const;
    void operator()(const CirclePrim& Circle) const;
    void operator()(const BarPrim& Bar) const;
    void operator()(const TextPrim& Label) const;

private:
    sf::Vector2f toTarget(Vec2 Point) const {
        return Screen ? sf::Vector2f{Point.X, Point.Y} : Camera::toDraw(Point);
    }

    sf::RenderTarget& Target;
    const Camera& Cam;
    const sf::Font& Font;
    bool Screen;
};

} // namespace

void RenderList::add(Layer Where, Primitive What) { Items.push_back({Where, std::move(What)}); }

void RenderList::addFallback(Layer Where, CapsulePrim Capsule) {
    ++FallbackCount;
    add(Where, Capsule);
}

std::vector<RenderItem> RenderList::getSorted() const {
    std::vector<RenderItem> Sorted = Items;
    std::ranges::stable_sort(Sorted, {}, &RenderItem::Where);
    return Sorted;
}

RenderStats RenderList::getStats() const {
    RenderStats Stats;
    Stats.Primitives = Items.size();
    Stats.Sprites = static_cast<size_t>(std::ranges::count_if(
        Items, [](const RenderItem& Item) { return std::holds_alternative<SpritePrim>(Item.What); }));
    Stats.Fallbacks = FallbackCount;
    return Stats;
}

void drawRenderList(sf::RenderTarget& Target, const Camera& Cam, const RenderList& List, Layer First, Layer Last,
                    const sf::Font& Font) {
    std::optional<bool> ScreenView;
    for (const RenderItem& Item : List.getSorted()) {
        if (Item.Where < First || Item.Where > Last) continue;
        const bool Screen = isScreenLayer(Item.Where);
        if (ScreenView != Screen) {
            Target.setView(Screen ? Cam.getScreenView() : Cam.getWorldView());
            ScreenView = Screen;
        }
        std::visit(PrimitiveDrawer(Target, Cam, Font, Screen), Item.What);
    }
}

namespace {

void PrimitiveDrawer::operator()(const SpritePrim& Sprite) const {
    if (!Sprite.Texture) return;
    const sf::Vector2u Size = Sprite.Texture->getSize();
    sf::Sprite Shape(*Sprite.Texture);
    Shape.setOrigin({Sprite.Origin.X * static_cast<float>(Size.x), Sprite.Origin.Y * static_cast<float>(Size.y)});
    Shape.setPosition(toTarget(Sprite.Position));
    // Y is flipped in the world view, so counter-clockwise becomes clockwise.
    Shape.setRotation(sf::radians(-Sprite.Angle));
    Shape.setScale({Sprite.Scale.X, Sprite.Scale.Y});
    Shape.setColor(Sprite.Tint);
    Target.draw(Shape);
}

void PrimitiveDrawer::operator()(const CapsulePrim& Capsule) const {
    const float Radius = std::max(std::min(Capsule.Size.X, Capsule.Size.Y) * 0.5f, 1e-4f);
    const float HalfStraight = std::max(Capsule.Size.Y * 0.5f - Radius, 0.0f);
    constexpr float Pi = std::numbers::pi_v<float>;

    // Local frame of the shape: X across, Y along the limb, pointing down as
    // in the draw coordinates. The top end goes over the arc from the left
    // side to the right one, the bottom end back.
    sf::ConvexShape Shape(CapsuleArcPoints * 2);
    for (size_t Index = 0; Index < CapsuleArcPoints; ++Index) {
        const float Angle = Pi + Pi * static_cast<float>(Index) / static_cast<float>(CapsuleArcPoints - 1);
        Shape.setPoint(Index, {Radius * std::cos(Angle), -HalfStraight + Radius * std::sin(Angle)});
        Shape.setPoint(Index + CapsuleArcPoints,
                       {-Radius * std::cos(Angle), HalfStraight - Radius * std::sin(Angle)});
    }
    Shape.setPosition(toTarget(Capsule.Position));
    Shape.setRotation(sf::radians(-Capsule.Angle));
    Shape.setFillColor(Capsule.Fill);
    Shape.setOutlineColor(Capsule.Outline);
    Shape.setOutlineThickness(Screen ? -1.0f : -CapsuleOutlineM);
    Target.draw(Shape);
}

void PrimitiveDrawer::operator()(const RectPrim& Rect) const {
    sf::RectangleShape Shape({Rect.Size.X, Rect.Size.Y});
    Shape.setOrigin({Rect.Size.X * 0.5f, Rect.Size.Y * 0.5f});
    Shape.setPosition(toTarget(Rect.Position));
    Shape.setRotation(sf::radians(-Rect.Angle));
    Shape.setFillColor(Rect.Fill);
    Shape.setOutlineColor(Rect.Outline);
    Shape.setOutlineThickness(-Rect.OutlineThickness);
    Target.draw(Shape);
}

void PrimitiveDrawer::operator()(const CirclePrim& Circle) const {
    sf::CircleShape Shape(Circle.Radius);
    Shape.setOrigin({Circle.Radius, Circle.Radius});
    Shape.setPosition(toTarget(Circle.Position));
    Shape.setFillColor(Circle.Fill);
    Target.draw(Shape);
}

void PrimitiveDrawer::operator()(const BarPrim& Bar) const {
    const sf::Vector2f TopLeft = toTarget(Bar.Position);
    sf::RectangleShape Back({Bar.Size.X, Bar.Size.Y});
    Back.setPosition(TopLeft);
    Back.setFillColor(Bar.Back);
    Back.setOutlineColor(Bar.Outline);
    Back.setOutlineThickness(1.0f);
    Target.draw(Back);

    const float Ratio = std::clamp(Bar.Ratio, 0.0f, 1.0f);
    sf::RectangleShape Fill({Bar.Size.X * Ratio, Bar.Size.Y});
    Fill.setPosition({Bar.FillFromRight ? TopLeft.x + Bar.Size.X * (1.0f - Ratio) : TopLeft.x, TopLeft.y});
    Fill.setFillColor(Bar.Fill);
    Target.draw(Fill);
}

void PrimitiveDrawer::operator()(const TextPrim& Label) const {
    // Text is always drawn in pixels, so that the font does not scale with
    // the world; a world-layer label goes to the point under its position.
    const sf::Vector2f At = Screen ? sf::Vector2f{Label.Position.X, Label.Position.Y} : Cam.worldToPixel(Label.Position);
    sf::Text Shape(Font, Label.Text, Label.SizePx);
    const sf::FloatRect Bounds = Shape.getLocalBounds();
    Shape.setPosition({At.x - Label.AlignX * Bounds.size.x - Bounds.position.x, At.y});
    Shape.setFillColor(Label.Fill);
    Shape.setOutlineColor(sf::Color::Black);
    Shape.setOutlineThickness(2.0f);
    if (!Screen) Target.setView(Cam.getScreenView());
    Target.draw(Shape);
    if (!Screen) Target.setView(Cam.getWorldView());
}

} // namespace

} // namespace fighter::render
