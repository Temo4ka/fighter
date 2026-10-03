//===- render/render_list.hpp - Generic drawing primitives ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares RenderList, the list of generic primitives a frame is
/// made of (docs/DEVELOPMENT_PLAN.md, "6. C. Render"): sprites, capsules,
/// rectangles, circles, bars and text, each on a layer.
///
/// The list knows nothing about the game: buildRenderList() (scene.hpp) turns
/// a fight into primitives, and drawRenderList() draws any list in one loop.
/// The draw order is the layer order, not the order of calls in code; within
/// a layer, primitives keep the order they were added in.
///
/// World layers are in meters with Y up (docs/ARCHITECTURE.md); the Hud layer
/// is in window pixels with Y down.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>

#include "core/vec2.hpp"
#include "render/camera.hpp"

namespace fighter::render {

/// Draw order, from the bottom (docs/ART.md, "Слои отрисовки").
enum class Layer : uint8_t {
    Background,
    Arena,
    FarFighter,
    NearFighter,
    Effects,
    Hud,          ///< Window pixels, not meters.
    Count
};

inline constexpr size_t LayerCount = static_cast<size_t>(Layer::Count);

/// A picture placed by a point of it. Scale converts picture pixels to
/// meters; a negative Scale.X mirrors the picture across its own Y axis.
struct SpritePrim {
    const sf::Texture* Texture = nullptr;
    Vec2 Position;                ///< Where Origin goes, m.
    float Angle = 0.0f;           ///< Radians, counter-clockwise.
    Vec2 Scale{1.0f, 1.0f};       ///< Meters per picture pixel.
    Vec2 Origin{0.5f, 0.5f};      ///< Fractions of the picture size from its top-left corner.
    sf::Color Tint = sf::Color::White;
};

/// A stadium of Size (X across, Y along) centered at Position: the stand-in
/// for a body part without a picture.
struct CapsulePrim {
    Vec2 Position;
    float Angle = 0.0f;
    Vec2 Size;
    sf::Color Fill;
    sf::Color Outline = sf::Color::Black;
};

/// A rectangle centered at Position.
struct RectPrim {
    Vec2 Position;
    Vec2 Size;
    float Angle = 0.0f;
    sf::Color Fill;
    sf::Color Outline = sf::Color::Transparent;
    float OutlineThickness = 0.0f;   ///< Inwards, in the units of the layer.
};

struct CirclePrim {
    Vec2 Position;
    float Radius = 0.0f;
    sf::Color Fill;
};

/// A gauge: Ratio of it is filled. Position is the top-left corner (Hud).
struct BarPrim {
    Vec2 Position;
    Vec2 Size;
    float Ratio = 1.0f;              ///< 0..1.
    bool FillFromRight = false;      ///< The fill shrinks towards the left edge.
    sf::Color Fill;
    sf::Color Back = sf::Color(0, 0, 0, 160);
    sf::Color Outline = sf::Color(230, 230, 230);
};

/// Text in the monospace font. Position is the top of the line; AlignX says
/// which point of the line it is: 0 the left end, 0.5 the middle, 1 the right.
struct TextPrim {
    std::string Text;
    Vec2 Position;
    unsigned SizePx = 16;
    float AlignX = 0.0f;
    sf::Color Fill = sf::Color::White;
};

using Primitive = std::variant<SpritePrim, CapsulePrim, RectPrim, CirclePrim, BarPrim, TextPrim>;

struct RenderItem {
    Layer Where = Layer::Arena;
    Primitive What;
};

/// What a frame consists of, for the debug panel.
struct RenderStats {
    size_t Primitives = 0;
    size_t Sprites = 0;
    size_t Fallbacks = 0;   ///< Body parts drawn as capsules for want of a picture.
};

class RenderList {
public:
    void add(Layer Where, Primitive What);
    /// Counts a body part drawn as a capsule because it has no picture.
    void addFallback(Layer Where, CapsulePrim Capsule);

    /// Primitives in draw order: by layer, then in the order they were added.
    std::vector<RenderItem> getSorted() const;
    const std::vector<RenderItem>& getItems() const { return Items; }
    RenderStats getStats() const;

private:
    std::vector<RenderItem> Items;
    size_t FallbackCount = 0;
};

/// Draws the primitives of layers [First, Last] in draw order. World layers
/// use Cam's world view, the Hud layer its screen view. The target is any
/// sf::RenderTarget, so a frame can go to a low-resolution texture as well.
void drawRenderList(sf::RenderTarget& Target, const Camera& Cam, const RenderList& List, Layer First, Layer Last,
                    const sf::Font& Font);

} // namespace fighter::render
