#include "render/debug_overlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

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
/// The panel starts below the HUD (bars and names, data/visuals.json "hud").
constexpr float PanelTop = 100.0f;
constexpr float PanelPadding = 6.0f;
constexpr float PanelColumnGap = 8.0f;
constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

sf::Color toSfColor(debug::Rgba Source) { return sf::Color(Source.R, Source.G, Source.B, Source.A); }

std::optional<size_t> findCategoryForKey(Scan Key) {
    for (size_t Index = 0; Index < CategoryKeys.size(); ++Index) {
        if (CategoryKeys[Index] == Key) return Index;
    }
    return std::nullopt;
}

/// Collects all line segments into one VertexArray: one draw call per frame.
class LineBatch {
public:
    void addLine(Vec2 From, Vec2 To, sf::Color Color) {
        Lines.append(sf::Vertex{Camera::toDraw(From), Color});
        Lines.append(sf::Vertex{Camera::toDraw(To), Color});
    }

    void addArc(Vec2 Center, float Radius, float A0, float A1, sf::Color Color) {
        const int Segments = std::max(4, static_cast<int>(CircleSegments * std::abs(A1 - A0) / TwoPi));
        Vec2 Prev = Center + Vec2{std::cos(A0), std::sin(A0)} * Radius;
        for (int Segment = 1; Segment <= Segments; ++Segment) {
            const float Angle = A0 + (A1 - A0) * static_cast<float>(Segment) / static_cast<float>(Segments);
            const Vec2 Next = Center + Vec2{std::cos(Angle), std::sin(Angle)} * Radius;
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
        case Scan::F7: return {true, DebugAction::CycleStyle};
        case Scan::P: return {true, DebugAction::TogglePause};
        case Scan::Period: return {true, DebugAction::Step};
        case Scan::LBracket: return {true, DebugAction::Slower};
        case Scan::RBracket: return {true, DebugAction::Faster};
        // Not R: it is a body kick of P1.
        case Scan::Backspace: return {true, DebugAction::Restart};
        default: return {};
    }
}

void DebugOverlay::drawBackdrop(sf::RenderTarget& Target, const Camera& Cam) const {
    Target.setView(Cam.getWorldView());
    const Vec2 Size = Cam.getViewSizeM();
    const Vec2 Center = Cam.getCenterM();

    sf::RectangleShape Bg({Size.X, Size.Y});
    Bg.setPosition(Camera::toDraw({Center.X - Size.X * 0.5f, Center.Y + Size.Y * 0.5f}));
    Bg.setFillColor(sf::Color(22, 24, 30));
    Target.draw(Bg);

    // A 1 m grid and the axes: scale and origin are visible at a glance.
    LineBatch Grid;
    const sf::Color Minor(255, 255, 255, 18);
    const sf::Color Axis(255, 255, 255, 60);
    const int X0 = static_cast<int>(std::floor(Center.X - Size.X * 0.5f));
    const int X1 = static_cast<int>(std::ceil(Center.X + Size.X * 0.5f));
    const int Y0 = static_cast<int>(std::floor(Center.Y - Size.Y * 0.5f));
    const int Y1 = static_cast<int>(std::ceil(Center.Y + Size.Y * 0.5f));
    for (int GridX = X0; GridX <= X1; ++GridX) {
        const float LineX = static_cast<float>(GridX);
        Grid.addLine({LineX, static_cast<float>(Y0)}, {LineX, static_cast<float>(Y1)}, GridX == 0 ? Axis : Minor);
    }
    for (int GridY = Y0; GridY <= Y1; ++GridY) {
        const float LineY = static_cast<float>(GridY);
        Grid.addLine({static_cast<float>(X0), LineY}, {static_cast<float>(X1), LineY}, GridY == 0 ? Axis : Minor);
    }
    Grid.draw(Target);
}

void DebugOverlay::drawPrimitives(sf::RenderTarget& Target, const Camera& Cam,
                                  const debug::DrawList& List) const {
    Target.setView(Cam.getWorldView());

    LineBatch Lines;
    for (const Primitive& Prim : List.getPrimitives()) {
        if (!isCategoryEnabled(Prim.Category)) continue;
        const sf::Color Color = toSfColor(debug::getColor(Prim.Category, Prim.Owner));

        switch (Prim.Kind) {
            case PrimitiveKind::Line:
                Lines.addLine(Prim.Anchor, Prim.End, Color);
                break;

            case PrimitiveKind::Arrow: {
                const Vec2 Tip = Prim.End;
                const Vec2 Shaft = Prim.End - Prim.Anchor;
                Lines.addLine(Prim.Anchor, Tip, Color);
                const float Len = Shaft.getLength();
                if (Len > 1e-4f) {
                    const Vec2 Dir = Shaft / Len;
                    const float Head = std::min(0.15f, Len * 0.3f);
                    const Vec2 Wing = perp(Dir) * Head * 0.5f;
                    Lines.addLine(Tip, Tip - Dir * Head + Wing, Color);
                    Lines.addLine(Tip, Tip - Dir * Head - Wing, Color);
                }
                break;
            }

            case PrimitiveKind::Circle:
                Lines.addArc(Prim.Anchor, Prim.Radius, 0.0f, TwoPi, Color);
                break;

            case PrimitiveKind::Arc:
                Lines.addArc(Prim.Anchor, Prim.Radius, Prim.Angle0, Prim.Angle1, Color);
                break;

            case PrimitiveKind::Poly: {
                const auto Points = List.getPoints(Prim);
                if (Points.size() >= 3) {
                    sf::ConvexShape Fill(Points.size());
                    for (size_t Index = 0; Index < Points.size(); ++Index)
                        Fill.setPoint(Index, Camera::toDraw(Points[Index]));
                    Fill.setFillColor(toSfColor(debug::getFillColor(Prim.Category, Prim.Owner)));
                    Target.draw(Fill);
                }
                for (size_t Index = 0; Index < Points.size(); ++Index)
                    Lines.addLine(Points[Index], Points[(Index + 1) % Points.size()], Color);
                break;
            }

            case PrimitiveKind::Point: {
                sf::CircleShape Dot(Prim.Radius);
                Dot.setOrigin({Prim.Radius, Prim.Radius});
                Dot.setPosition(Camera::toDraw(Prim.Anchor));
                Dot.setFillColor(Color);
                Target.draw(Dot);
                break;
            }

            case PrimitiveKind::Cross: {
                const float Size = Prim.Radius;
                Lines.addLine(Prim.Anchor - Vec2{Size, 0.0f}, Prim.Anchor + Vec2{Size, 0.0f}, Color);
                Lines.addLine(Prim.Anchor - Vec2{0.0f, Size}, Prim.Anchor + Vec2{0.0f, Size}, Color);
                Lines.addArc(Prim.Anchor, Size * 0.45f, 0.0f, TwoPi, sf::Color::Black);
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
    for (const Primitive& Prim : List.getPrimitives()) {
        if (!isCategoryEnabled(Prim.Category)) continue;
        const std::string_view Label = List.getText(Prim);
        if (Label.empty()) continue;

        const Vec2 Anchor = (Prim.Kind == PrimitiveKind::Arrow) ? Prim.End : Prim.Anchor;
        sf::Text Text(Font, std::string(Label), FontSize);
        Text.setPosition(Cam.worldToPixel(Anchor) + sf::Vector2f{4.0f, -LineHeight});
        Text.setFillColor(toSfColor(debug::getColor(Prim.Category, Prim.Owner)));
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
    Lines.push_back({"P pause  . step  [ ] speed  Backspace restart  F5 reload  F6 showcase  F7 style", sf::Color(170, 170, 170)});
    Lines.push_back({""});

    for (const auto& [Key, Value] : List.getPanel()) Lines.push_back({std::format("{:<14} {}", Key, Value)});
    Lines.push_back({""});

    for (size_t Index = 0; Index < debug::CatCount; ++Index) {
        const auto Category = static_cast<Cat>(Index);
        const bool On = Enabled.test(Index);
        sf::Color Color = toSfColor(debug::getColor(Category, debug::Side::Left));
        Color.a = On ? 255 : 90;
        Lines.push_back({std::format("{} [{}] {}", CategoryKeyNames[Index], On ? 'x' : ' ', debug::getCatName(Category)),
                         Color});
    }

    if (!List.getEvents().empty()) {
        Lines.push_back({""});
        Lines.push_back({"events:", sf::Color(255, 220, 120)});
        for (const std::string& Event : List.getEvents()) Lines.push_back({Event});
    }

    // The lines flow into columns when they do not fit the window height,
    // each column on its own translucent backing.
    const float WindowHeight = static_cast<float>(Cam.getWindowSize().y);
    const size_t RowsPerColumn = std::max<size_t>(
        1, static_cast<size_t>((WindowHeight - PanelTop - 2.0f * PanelPadding) / LineHeight));
    float ColumnLeft = 8.0f;
    for (size_t First = 0; First < Lines.size(); First += RowsPerColumn) {
        const size_t Last = std::min(First + RowsPerColumn, Lines.size());
        float Width = 0.0f;
        std::vector<sf::Text> Texts;
        Texts.reserve(Last - First);
        for (size_t Index = First; Index < Last; ++Index) {
            sf::Text& Line = Texts.emplace_back(Font, Lines[Index].Text, FontSize);
            Line.setFillColor(Lines[Index].Color);
            Line.setPosition({ColumnLeft + 8.0f,
                              PanelTop + PanelPadding + LineHeight * static_cast<float>(Index - First)});
            Width = std::max(Width, Line.getLocalBounds().size.x);
        }
        sf::RectangleShape Back({Width + 16.0f, LineHeight * static_cast<float>(Last - First) + 2.0f * PanelPadding});
        Back.setPosition({ColumnLeft, PanelTop});
        Back.setFillColor(sf::Color(0, 0, 0, 170));
        Target.draw(Back);
        for (const sf::Text& Line : Texts) Target.draw(Line);
        ColumnLeft += Width + 16.0f + PanelColumnGap;
    }
}

} // namespace fighter::render
