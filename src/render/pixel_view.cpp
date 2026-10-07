#include "render/pixel_view.hpp"

#include <algorithm>
#include <cmath>

namespace fighter::render {
namespace {

unsigned divideRoundingUp(unsigned Value, unsigned Divisor);

} // namespace

PixelLayout computePixelLayout(sf::Vector2u WindowPx, float ViewHeightM, float PixelsPerMeter, int Scale,
                               bool Letterbox) {
    const sf::Vector2u Window{std::max(WindowPx.x, 1u), std::max(WindowPx.y, 1u)};
    const auto ViewHeightPx = std::max(static_cast<unsigned>(std::lround(ViewHeightM * PixelsPerMeter)), 1u);
    const unsigned Fitting = std::max(Window.y / ViewHeightPx, 1u);

    PixelLayout Layout;
    Layout.PixelsPerMeter = PixelsPerMeter;
    if (Scale <= 0) {
        Layout.Scale = Fitting;
    } else {
        Layout.Scale = Letterbox ? std::min(static_cast<unsigned>(Scale), Fitting) : static_cast<unsigned>(Scale);
    }

    if (Letterbox) {
        Layout.LowResSizePx = {std::max(Window.x / Layout.Scale, 1u), ViewHeightPx};
    } else {
        Layout.LowResSizePx = {divideRoundingUp(Window.x, Layout.Scale), divideRoundingUp(Window.y, Layout.Scale)};
    }
    const auto ScaledW = static_cast<int>(Layout.LowResSizePx.x * Layout.Scale);
    const auto ScaledH = static_cast<int>(Layout.LowResSizePx.y * Layout.Scale);
    Layout.OffsetPx = {(static_cast<int>(Window.x) - ScaledW) / 2, (static_cast<int>(Window.y) - ScaledH) / 2};
    return Layout;
}

Vec2 snapToPixelGrid(Vec2 CenterM, sf::Vector2u SizePx, float PixelsPerMeter) {
    const float HalfW = static_cast<float>(SizePx.x) * 0.5f / PixelsPerMeter;
    const float HalfH = static_cast<float>(SizePx.y) * 0.5f / PixelsPerMeter;
    const float Left = std::round((CenterM.X - HalfW) * PixelsPerMeter) / PixelsPerMeter;
    const float Top = std::round((CenterM.Y + HalfH) * PixelsPerMeter) / PixelsPerMeter;
    return {Left + HalfW, Top - HalfH};
}

Camera makeLowResCamera(const PixelLayout& Layout, Vec2 CenterM) {
    Camera::Config Settings;
    Settings.ViewHeightM = static_cast<float>(Layout.LowResSizePx.y) / Layout.PixelsPerMeter;
    Settings.CenterM = snapToPixelGrid(CenterM, Layout.LowResSizePx, Layout.PixelsPerMeter);
    Camera Cam(Settings);
    Cam.setWindowSize(Layout.LowResSizePx);
    return Cam;
}

Camera makeDisplayCamera(const PixelLayout& Layout, const Camera& LowRes, sf::Vector2u WindowPx) {
    const sf::Vector2u Window{std::max(WindowPx.x, 1u), std::max(WindowPx.y, 1u)};
    const float WindowPpm = static_cast<float>(Layout.Scale) * Layout.PixelsPerMeter;
    // The world point under the window center, through the scaled picture.
    const Vec2 TopLeft = LowRes.pixelToWorld({0.0f, 0.0f});
    const float CenterX = static_cast<float>(Window.x) * 0.5f - static_cast<float>(Layout.OffsetPx.x);
    const float CenterY = static_cast<float>(Window.y) * 0.5f - static_cast<float>(Layout.OffsetPx.y);

    Camera::Config Settings;
    Settings.ViewHeightM = static_cast<float>(Window.y) / WindowPpm;
    Settings.CenterM = {TopLeft.X + CenterX / WindowPpm, TopLeft.Y - CenterY / WindowPpm};
    Camera Cam(Settings);
    Cam.setWindowSize(Window);
    return Cam;
}

namespace {

unsigned divideRoundingUp(unsigned Value, unsigned Divisor) { return (Value + Divisor - 1) / Divisor; }

} // namespace

} // namespace fighter::render
