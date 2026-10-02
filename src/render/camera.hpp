//===- render/camera.hpp - World to screen conversion -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares render::Camera, the only place in the project where
/// world coordinates become pixels (docs/DEVELOPMENT_PLAN.md, section 3.1).
///
/// World: meters, Y up, (0, 0) is the arena center at floor level.
/// Screen: pixels, Y down, (0, 0) is the top-left corner of the window.
///
/// There are two ways to draw in world units:
///  - set getWorldView() as the view and pass SFML the coordinates from
///    toDraw(WorldPos);
///  - convert a point to pixels with worldToPixel() and draw in
///    getScreenView(). Text is drawn this way so that it does not scale with
///    the world.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>

#include "core/vec2.hpp"

namespace fighter::render {

class Camera {
public:
    struct Config {
        float ViewHeightM = 6.0f;      ///< How many meters of world fit vertically.
        Vec2 CenterM{0.0f, 2.5f};      ///< View center: the floor is 0.5 m above the bottom edge.
    };

    Camera() : Camera(Config{}) {}
    explicit Camera(Config Settings);

    /// The view width follows the window aspect ratio; the height in meters is kept.
    void setWindowSize(sf::Vector2u SizePx);

    sf::Vector2f worldToPixel(Vec2 WorldPos) const;
    Vec2 pixelToWorld(sf::Vector2f PixelPos) const;

    float getPixelsPerMeter() const;
    Vec2 getViewSizeM() const { return {ViewHeightM * getAspect(), ViewHeightM}; }
    Vec2 getCenterM() const { return Center; }
    sf::Vector2u getWindowSize() const { return WindowPx; }

    /// SFML view in world units. SFML's Y axis points down, so points go
    /// through toDraw(): we flip Y of the coordinates rather than of the view,
    /// otherwise textures would be drawn upside down.
    sf::View getWorldView() const;
    static sf::Vector2f toDraw(Vec2 WorldPos) { return {WorldPos.X, -WorldPos.Y}; }

    /// View in window pixels (for the HUD and text). SFML does not update the
    /// window's default view after a resize, so it is taken from here.
    sf::View getScreenView() const;

private:
    float getAspect() const;

    float ViewHeightM;
    Vec2 Center;
    sf::Vector2u WindowPx{1280, 720};
};

} // namespace fighter::render
