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

// Клавиши категорий: 1…9, 0, «-» — в порядке перечисления debug::Cat.
constexpr std::array<Scan, debug::kCatCount> kCategoryKeys = {
    Scan::Num1, Scan::Num2, Scan::Num3, Scan::Num4, Scan::Num5, Scan::Num6,
    Scan::Num7, Scan::Num8, Scan::Num9, Scan::Num0, Scan::Hyphen,
};
constexpr std::array<const char*, debug::kCatCount> kCategoryKeyNames = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-",
};

constexpr unsigned kFontSize = 13;
constexpr float kLineHeight = 16.0f;
constexpr int kCircleSegments = 32;

sf::Color toSf(debug::Rgba c) { return sf::Color(c.r, c.g, c.b, c.a); }

std::optional<std::size_t> categoryForKey(Scan key) {
    for (std::size_t i = 0; i < kCategoryKeys.size(); ++i) {
        if (kCategoryKeys[i] == key) return i;
    }
    return std::nullopt;
}

// Все отрезки собираются в один VertexArray — один вызов отрисовки на кадр.
class LineBatch {
public:
    void add(Vec2 a, Vec2 b, sf::Color color) {
        lines_.append(sf::Vertex{Camera::toDraw(a), color});
        lines_.append(sf::Vertex{Camera::toDraw(b), color});
    }

    void addCircle(Vec2 c, float r, float a0, float a1, sf::Color color) {
        const int segments = std::max(4, static_cast<int>(kCircleSegments * std::abs(a1 - a0) / (2.0f * std::numbers::pi_v<float>)));
        Vec2 prev = c + Vec2{std::cos(a0), std::sin(a0)} * r;
        for (int i = 1; i <= segments; ++i) {
            const float t = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(segments);
            const Vec2 next = c + Vec2{std::cos(t), std::sin(t)} * r;
            add(prev, next, color);
            prev = next;
        }
    }

    void draw(sf::RenderTarget& target) const { target.draw(lines_); }

private:
    sf::VertexArray lines_{sf::PrimitiveType::Lines};
};

} // namespace

DebugOverlay::DebugOverlay(Resources& resources) : resources_(resources) {
    enabled_.set();
}

DebugOverlay::KeyResult DebugOverlay::handleKey(sf::Keyboard::Scancode key) {
    if (auto cat = categoryForKey(key)) {
        enabled_.flip(*cat);
        return {true, DebugAction::None};
    }
    switch (key) {
        case Scan::F1: mode_ = ViewMode::DebugOnly; return {true, DebugAction::None};
        case Scan::F2: mode_ = ViewMode::Both; return {true, DebugAction::None};
        case Scan::F3: mode_ = ViewMode::TexturesOnly; return {true, DebugAction::None};
        case Scan::F4: panelVisible_ = !panelVisible_; return {true, DebugAction::None};
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

void DebugOverlay::drawBackdrop(sf::RenderTarget& target, const Camera& camera) const {
    target.setView(camera.worldView());
    const Vec2 size = camera.viewSizeM();
    const Vec2 c = camera.centerM();

    sf::RectangleShape bg({size.x, size.y});
    bg.setPosition(Camera::toDraw({c.x - size.x * 0.5f, c.y + size.y * 0.5f}));
    bg.setFillColor(sf::Color(22, 24, 30));
    target.draw(bg);

    // Сетка 1 м и оси: сразу видно масштаб и где ноль.
    LineBatch grid;
    const sf::Color minor(255, 255, 255, 18);
    const sf::Color axis(255, 255, 255, 60);
    const int x0 = static_cast<int>(std::floor(c.x - size.x * 0.5f));
    const int x1 = static_cast<int>(std::ceil(c.x + size.x * 0.5f));
    const int y0 = static_cast<int>(std::floor(c.y - size.y * 0.5f));
    const int y1 = static_cast<int>(std::ceil(c.y + size.y * 0.5f));
    for (int x = x0; x <= x1; ++x) {
        grid.add({static_cast<float>(x), static_cast<float>(y0)}, {static_cast<float>(x), static_cast<float>(y1)},
                 x == 0 ? axis : minor);
    }
    for (int y = y0; y <= y1; ++y) {
        grid.add({static_cast<float>(x0), static_cast<float>(y)}, {static_cast<float>(x1), static_cast<float>(y)},
                 y == 0 ? axis : minor);
    }
    grid.draw(target);
}

void DebugOverlay::drawPrimitives(sf::RenderTarget& target, const Camera& camera,
                                  const debug::DrawList& list) const {
    target.setView(camera.worldView());

    LineBatch lines;
    for (const Primitive& p : list.primitives()) {
        if (!categoryEnabled(p.cat)) continue;
        const sf::Color color = toSf(debug::color(p.cat, p.side));

        switch (p.kind) {
            case Primitive::Kind::Line:
                lines.add(p.a, p.b, color);
                break;

            case Primitive::Kind::Arrow: {
                const Vec2 tip = p.a + p.b;
                lines.add(p.a, tip, color);
                const float len = p.b.length();
                if (len > 1e-4f) {
                    const Vec2 dir = p.b / len;
                    const float head = std::min(0.15f, len * 0.3f);
                    const Vec2 side = perp(dir) * head * 0.5f;
                    lines.add(tip, tip - dir * head + side, color);
                    lines.add(tip, tip - dir * head - side, color);
                }
                break;
            }

            case Primitive::Kind::Circle:
                lines.addCircle(p.a, p.radius, 0.0f, 2.0f * std::numbers::pi_v<float>, color);
                break;

            case Primitive::Kind::Arc:
                lines.addCircle(p.a, p.radius, p.angle0, p.angle1, color);
                break;

            case Primitive::Kind::Poly: {
                const auto pts = list.points(p);
                if (pts.size() >= 3) {
                    sf::ConvexShape fill(pts.size());
                    for (std::size_t i = 0; i < pts.size(); ++i) fill.setPoint(i, Camera::toDraw(pts[i]));
                    fill.setFillColor(toSf(debug::fillColor(p.cat, p.side)));
                    target.draw(fill);
                }
                for (std::size_t i = 0; i < pts.size(); ++i) lines.add(pts[i], pts[(i + 1) % pts.size()], color);
                break;
            }

            case Primitive::Kind::Point: {
                sf::CircleShape dot(p.radius);
                dot.setOrigin({p.radius, p.radius});
                dot.setPosition(Camera::toDraw(p.a));
                dot.setFillColor(color);
                target.draw(dot);
                break;
            }

            case Primitive::Kind::Cross: {
                const float s = p.radius;
                lines.add(p.a - Vec2{s, 0.0f}, p.a + Vec2{s, 0.0f}, color);
                lines.add(p.a - Vec2{0.0f, s}, p.a + Vec2{0.0f, s}, color);
                lines.addCircle(p.a, s * 0.45f, 0.0f, 2.0f * std::numbers::pi_v<float>, sf::Color::Black);
                break;
            }

            case Primitive::Kind::Text:
                break;   // текст — ниже, в пикселях
        }
    }
    lines.draw(target);

    // Подписи рисуются в пикселях, чтобы шрифт не масштабировался вместе с миром.
    target.setView(camera.screenView());
    const sf::Font& font = resources_.font(assets::kMonoFont);
    for (const Primitive& p : list.primitives()) {
        if (!categoryEnabled(p.cat)) continue;
        const std::string_view label = list.text(p);
        if (label.empty()) continue;

        const Vec2 anchor = (p.kind == Primitive::Kind::Arrow) ? p.a + p.b : p.a;
        sf::Text text(font, std::string(label), kFontSize);
        text.setPosition(camera.worldToPixel(anchor) + sf::Vector2f{4.0f, -kLineHeight});
        text.setFillColor(toSf(debug::color(p.cat, p.side)));
        text.setOutlineColor(sf::Color(0, 0, 0, 200));
        text.setOutlineThickness(1.0f);
        target.draw(text);
    }
}

void DebugOverlay::drawPanel(sf::RenderTarget& target, const Camera& camera, const debug::DrawList& list) const {
    if (!panelVisible_) return;
    target.setView(camera.screenView());
    const sf::Font& font = resources_.font(assets::kMonoFont);

    struct Line {
        std::string text;
        sf::Color color = sf::Color(230, 230, 230);
    };
    std::vector<Line> lines;

    const char* modeName = mode_ == ViewMode::DebugOnly ? "debug" : mode_ == ViewMode::Both ? "both" : "textures";
    lines.push_back({std::format("DEBUG  mode: {}  (F1 debug, F2 both, F3 textures, F4 panel)", modeName),
                     sf::Color(255, 220, 120)});
    lines.push_back({"P pause  . step  [ ] speed  R restart  F5 reload  F6 showcase", sf::Color(170, 170, 170)});
    lines.push_back({""});

    for (const auto& [key, value] : list.panel()) lines.push_back({std::format("{:<14} {}", key, value)});
    lines.push_back({""});

    for (std::size_t i = 0; i < debug::kCatCount; ++i) {
        const auto cat = static_cast<Cat>(i);
        const bool on = enabled_.test(i);
        sf::Color c = toSf(debug::color(cat, debug::Side::Left));
        c.a = on ? 255 : 90;
        lines.push_back({std::format("{} [{}] {}", kCategoryKeyNames[i], on ? 'x' : ' ', debug::catName(cat)), c});
    }

    if (!list.events().empty()) {
        lines.push_back({""});
        lines.push_back({"events:", sf::Color(255, 220, 120)});
        for (const std::string& e : list.events()) lines.push_back({e});
    }

    // Полупрозрачная подложка под текстом.
    float width = 0.0f;
    std::vector<sf::Text> texts;
    texts.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        sf::Text& t = texts.emplace_back(font, lines[i].text, kFontSize);
        t.setFillColor(lines[i].color);
        t.setPosition({16.0f, 64.0f + kLineHeight * static_cast<float>(i)});
        width = std::max(width, t.getLocalBounds().size.x);
    }
    sf::RectangleShape back({width + 16.0f, kLineHeight * static_cast<float>(lines.size()) + 12.0f});
    back.setPosition({8.0f, 58.0f});
    back.setFillColor(sf::Color(0, 0, 0, 170));
    target.draw(back);
    for (const sf::Text& t : texts) target.draw(t);
}

} // namespace fighter::render
