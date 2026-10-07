//===- render/pixel_view.hpp - Pixel render mode layout ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the geometry of the pixel render mode (T.4): the world
/// is drawn into a low-resolution picture with nearest-neighbour sampling and
/// that picture is scaled up a whole number of times into the window. A body
/// part rotated to any angle is then made of square pixels of the same size
/// as everything else on screen.
///
/// The low-resolution camera is snapped to its pixel grid, so a moving view
/// (camera shake included) moves the picture by whole pixels instead of
/// making the pixel art shimmer.
///
/// Everything here is plain arithmetic; the drawing lives in BattleRenderer.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <SFML/System/Vector2.hpp>

#include "core/vec2.hpp"
#include "render/camera.hpp"

namespace fighter::render {

/// Where the low-resolution picture goes in the window.
struct PixelLayout {
    float PixelsPerMeter = 32.0f;     ///< Low-resolution pixels per meter.
    unsigned Scale = 1;               ///< Window pixels per low-resolution pixel.
    sf::Vector2u LowResSizePx{1, 1};
    /// The top-left corner of the scaled picture in the window; negative when
    /// the picture is cropped by the window edges.
    sf::Vector2i OffsetPx{0, 0};
};

/// Picks the integer scale and the low-resolution size for a window of
/// \p WindowPx showing \p ViewHeightM meters of world.
///
/// \p Scale 0 takes the largest scale at which the view height fits the
/// window (at least 1); a positive \p Scale is used as is, but with
/// \p Letterbox it is lowered to the fitting one. With \p Letterbox the view
/// height in meters is kept and the picture is centred with black bars;
/// without it the picture fills the window and the view grows to cover it.
PixelLayout computePixelLayout(sf::Vector2u WindowPx, float ViewHeightM, float PixelsPerMeter, int Scale,
                               bool Letterbox);

/// Moves \p CenterM the least so that the left and top edges of a view of
/// \p SizePx pixels at \p PixelsPerMeter lie on the pixel grid of the world.
Vec2 snapToPixelGrid(Vec2 CenterM, sf::Vector2u SizePx, float PixelsPerMeter);

/// The camera that draws the world into the low-resolution picture, centred
/// as close to \p CenterM as the pixel grid allows.
Camera makeLowResCamera(const PixelLayout& Layout, Vec2 CenterM);

/// A window-resolution camera that shows the world exactly where the scaled
/// low-resolution picture of \p LowRes shows it. Debug primitives and labels
/// are drawn with it on top of the picture, sharp and readable.
Camera makeDisplayCamera(const PixelLayout& Layout, const Camera& LowRes, sf::Vector2u WindowPx);

} // namespace fighter::render
