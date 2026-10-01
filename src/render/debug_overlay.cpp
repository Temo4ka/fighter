#include "render/debug_overlay.hpp"

#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include "debug/palette.hpp"
#include "render/assets.hpp"

namespace fighter::render {
namespace {

using Scan = sf::Keyboard::Scan;
using debug::Cat;
using debug::Primitive;
using debug::PrimitiveKind;

// Category keys: 1...9, 0, '-' in the order of debug::Cat.
constexpr std::array<Scan, debug::CatCount> CategoryKeys = {
    Scan::Num1, Scan::Num2, Scan::Num3, Scan::Num4, Scan::Num5, Scan::Num6,
    Scan::Num7, Scan::Num8, Scan::Num9, Scan::Num0, Scan::Hyphen,
};
constexpr std::array<const char*, debug::CatCount> CategoryKeyNames = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-",
};

constexpr unsigned FontSize = 13;
constexpr float LineHeight = 16.0f;
constexpr int CircleSegments = 32;
constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

sf::Color toSfColor(debug::Rgba C) { return sf::Color(C.R, C.G, C.B, C.A); }

std::optional<std::size_t> findCategoryForKey(Scan Key) {
    for (std::size_t I = 0; I < CategoryKeys.size(); ++I) {
        if (CategoryKeys[I] == Key) return I;
    }
    return std::nullopt;
}

/// Collects all line segments into one VertexArray: one draw call per frame.
class LineBatch {
public:
    void addLine(Vec2 A, Vec2 B, sf::Color Color) {
        Lines.append(sf::Vertex{Camera::toDraw(A), Color});
        Lines.append(sf::Vertex{Camera::toDraw(B), Color});
    }

    void addArc(Vec2 Center, float Radius, float A0, float A1, sf::Color Color) {
        const int Segments = std::max(4, static_cast<int>(CircleSegments * std::abs(A1 - A0) / TwoPi));
        Vec2 Prev = Center + Vec2{std::cos(A0), std::sin(A0)} * Radius;
        for (int I = 1; I <= Segments; ++I) {
            const float T = A0 + (A1 - A0) * static_cast<float>(I) / static_cast<float>(Segments);
            const Vec2 Next = Center + Vec2{std::cos(T), std::sin(T)} * Radius;
            addLine(Prev, Next, Color);
            Prev = Next;
        }
    }

    void draw(sf::RenderTarget& Target) const { Target.draw(Lines); }

private:
    sf::VertexArray Lines{sf::PrimitiveType::Lines};
};

} // namespace

DebugOverlay::DebugOverlay(Resources& Res) : Assets(Res) {
    Enabled.set();
}

DebugOverlay::KeyResult DebugOverlay::handleKey(sf::Keyboard::Scancode Key) {
    if (auto Category = findCategoryForKey(Key)) {
        Enabled.flip(*Category);
        return {true, DebugAction::None};
    }
    switch (Key) {
        case Scan::F1: Mode = ViewMode::DebugOnly; return {true, DebugAction::None};
        case Scan::F2: Mode = ViewMode::Both; return {true, DebugAction::None};
        case Scan::F3: Mode = ViewMode::TexturesOnly; return {true, DebugAction::None};
        case Scan::F4: PanelVisible = !PanelVisible; return {true, DebugAction::None};
        case Scan::F5: return {true, DebugAction::Reload};
        case Scan::F6: return {true, DebugAction::ToggleShowcase};
        case Scan::P: return {true, DebugAction::TogglePause};
        case Scan::Period: return {true, DebugAction::Step};
        case Scan::LBracket: return {true, DebugAction::Slower};
        case Scan::RBracket: return {true, DebugAction::Faster};
        case Scan::R: return {true, DebugAction::Restart};
        default: return {};
    }
}

void DebugOverlay::drawBackdrop(sf::RenderTarget& Target, const Camera& Cam) const {
    Target.setView(Cam.getWorldView());
    const Vec2 Size = Cam.getViewSizeM();
    const Vec2 C = Cam.getCenterM();

    sf::RectangleShape Bg({Size.X, Size.Y});
    Bg.setPosition(Camera::toDraw({C.X - Size.X * 0.5f, C.Y + Size.Y * 0.5f}));
    Bg.setFillColor(sf::Color(22, 24, 30));
    Target.draw(Bg);

    // A 1 m grid and the axes: scale and origin are visible at a glance.
    LineBatch Grid;
    const sf::Color Minor(255, 255, 255, 18);
    const sf::Color Axis(255, 255, 255, 60);
    const int X0 = static_cast<int>(std::floor(C.X - Size.X * 0.5f));
    const int X1 = static_cast<int>(std::ceil(C.X + Size.X * 0.5f));
    const int Y0 = static_cast<int>(std::floor(C.Y - Size.Y * 0.5f));
    const int Y1 = static_cast<int>(std::ceil(C.Y + Size.Y * 0.5f));
    for (int X = X0; X <= X1; ++X) {
        Grid.addLine({static_cast<float>(X), static_cast<float>(Y0)}, {static_cast<float>(X), static_cast<float>(Y1)},
                     X == 0 ? Axis : Minor);
    }
    for (int Y = Y0; Y <= Y1; ++Y) {
        Grid.addLine({static_cast<float>(X0), static_cast<float>(Y)}, {static_cast<float>(X1), static_cast<float>(Y)},
                     Y == 0 ? Axis : Minor);
    }
    Grid.draw(Target);
}

void DebugOverlay::drawPrimitives(sf::RenderTarget& Target, const Camera& Cam,
                                  const debug::DrawList& List) const {
    Target.setView(Cam.getWorldView());

    LineBatch Lines;
    for (const Primitive& P : List.getPrimitives()) {
        if (!isCategoryEnabled(P.Category)) continue;
        const sf::Color Color = toSfColor(debug::getColor(P.Category, P.Owner));

        switch (P.Kind) {
            case PrimitiveKind::Line:
                Lines.addLine(P.A, P.B, Color);
                break;

            case PrimitiveKind::Arrow: {
                const Vec2 Tip = P.A + P.B;
                Lines.addLine(P.A, Tip, Color);
                const float Len = P.B.getLength();
                if (Len > 1e-4f) {
                    const Vec2 Dir = P.B / Len;
                    const float Head = std::min(0.15f, Len * 0.3f);
                    const Vec2 Wing = perp(Dir) * Head * 0.5f;
                    Lines.addLine(Tip, Tip - Dir * Head + Wing, Color);
                    Lines.addLine(Tip, Tip - Dir * Head - Wing, Color);
                }
                break;
            }

            case PrimitiveKind::Circle:
                Lines.addArc(P.A, P.Radius, 0.0f, TwoPi, Color);
                break;

            case PrimitiveKind::Arc:
                Lines.addArc(P.A, P.Radius, P.Angle0, P.Angle1, Color);
                break;

            case PrimitiveKind::Poly: {
                const auto Points = List.getPoints(P);
                if (Points.size() >= 3) {
                    sf::ConvexShape Fill(Points.size());
                    for (std::size_t I = 0; I < Points.size(); ++I) Fill.setPoint(I, Camera::toDraw(Points[I]));
                    Fill.setFillColor(toSfColor(debug::getFillColor(P.Category, P.Owner)));
                    Target.draw(Fill);
                }
                for (std::size_t I = 0; I < Points.size(); ++I)
                    Lines.addLine(Points[I], Points[(I + 1) % Points.size()], Color);
                break;
            }

            case PrimitiveKind::Point: {
                sf::CircleShape Dot(P.Radius);
                Dot.setOrigin({P.Radius, P.Radius});
                Dot.setPosition(Camera::toDraw(P.A));
                Dot.setFillColor(Color);
                Target.draw(Dot);
                break;
            }

            case PrimitiveKind::Cross: {
                const float S = P.Radius;
                Lines.addLine(P.A - Vec2{S, 0.0f}, P.A + Vec2{S, 0.0f}, Color);
                Lines.addLine(P.A - Vec2{0.0f, S}, P.A + Vec2{0.0f, S}, Color);
                Lines.addArc(P.A, S * 0.45f, 0.0f, TwoPi, sf::Color::Black);
                break;
            }

            case PrimitiveKind::Text:
                break;   // Text is drawn below, in pixels.
        }
    }
    Lines.draw(Target);

    // Labels are drawn in pixels so that the font does not scale with the world.
    Target.setView(Cam.getScreenView());
    const sf::Font& Font = Assets.getFont(assets::MonoFontPath);
    for (const Primitive& P : List.getPrimitives()) {
        if (!isCategoryEnabled(P.Category)) continue;
        const std::string_view Label = List.getText(P);
        if (Label.empty()) continue;

        const Vec2 Anchor = (P.Kind == PrimitiveKind::Arrow) ? P.A + P.B : P.A;
        sf::Text Text(Font, std::string(Label), FontSize);
        Text.setPosition(Cam.worldToPixel(Anchor) + sf::Vector2f{4.0f, -LineHeight});
        Text.setFillColor(toSfColor(debug::getColor(P.Category, P.Owner)));
        Text.setOutlineColor(sf::Color(0, 0, 0, 200));
        Text.setOutlineThickness(1.0f);
        Target.draw(Text);
    }
}

void DebugOverlay::drawPanel(sf::RenderTarget& Target, const Camera& Cam, const debug::DrawList& List) const {
    if (!PanelVisible) return;
    Target.setView(Cam.getScreenView());
    const sf::Font& Font = Assets.getFont(assets::MonoFontPath);

    struct PanelLine {
        std::string Text;
        sf::Color Color = sf::Color(230, 230, 230);
    };
    std::vector<PanelLine> Lines;

    const char* ModeName = Mode == ViewMode::DebugOnly ? "debug" : Mode == ViewMode::Both ? "both" : "textures";
    Lines.push_back({std::format("DEBUG  mode: {}  (F1 debug, F2 both, F3 textures, F4 panel)", ModeName),
                     sf::Color(255, 220, 120)});
    Lines.push_back({"P pause  . step  [ ] speed  R restart  F5 reload  F6 showcase", sf::Color(170, 170, 170)});
    Lines.push_back({""});

    for (const auto& [Key, Value] : List.getPanel()) Lines.push_back({std::format("{:<14} {}", Key, Value)});
    Lines.push_back({""});

    for (std::size_t I = 0; I < debug::CatCount; ++I) {
        const auto C = static_cast<Cat>(I);
        const bool On = Enabled.test(I);
        sf::Color Color = toSfColor(debug::getColor(C, debug::Side::Left));
        Color.a = On ? 255 : 90;
        Lines.push_back({std::format("{} [{}] {}", CategoryKeyNames[I], On ? 'x' : ' ', debug::getCatName(C)), Color});
    }

    if (!List.getEvents().empty()) {
        Lines.push_back({""});
        Lines.push_back({"events:", sf::Color(255, 220, 120)});
        for (const std::string& E : List.getEvents()) Lines.push_back({E});
    }

    // Translucent backing under the text.
    float Width = 0.0f;
    std::vector<sf::Text> Texts;
    Texts.reserve(Lines.size());
    for (std::size_t I = 0; I < Lines.size(); ++I) {
        sf::Text& T = Texts.emplace_back(Font, Lines[I].Text, FontSize);
        T.setFillColor(Lines[I].Color);
        T.setPosition({16.0f, 64.0f + LineHeight * static_cast<float>(I)});
        Width = std::max(Width, T.getLocalBounds().size.x);
    }
    sf::RectangleShape Back({Width + 16.0f, LineHeight * static_cast<float>(Lines.size()) + 12.0f});
    Back.setPosition({8.0f, 58.0f});
    Back.setFillColor(sf::Color(0, 0, 0, 170));
    Target.draw(Back);
    for (const sf::Text& T : Texts) Target.draw(T);
}

} // namespace fighter::render
