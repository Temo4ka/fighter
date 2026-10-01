#include "render/camera.hpp"

#include <algorithm>

namespace fighter::render {

Camera::Camera(Config Settings) : ViewHeightM(Settings.ViewHeightM), Center(Settings.CenterM) {}

void Camera::setWindowSize(sf::Vector2u SizePx) {
    WindowPx = {std::max(SizePx.x, 1u), std::max(SizePx.y, 1u)};
}

float Camera::getAspect() const {
    return static_cast<float>(WindowPx.x) / static_cast<float>(WindowPx.y);
}

float Camera::getPixelsPerMeter() const {
    return static_cast<float>(WindowPx.y) / ViewHeightM;
}

sf::Vector2f Camera::worldToPixel(Vec2 WorldPos) const {
    const Vec2 Size = getViewSizeM();
    const float Left = Center.X - Size.X * 0.5f;
    const float Top = Center.Y + Size.Y * 0.5f;
    const float Ppm = getPixelsPerMeter();
    return {(WorldPos.X - Left) * Ppm, (Top - WorldPos.Y) * Ppm};
}

Vec2 Camera::pixelToWorld(sf::Vector2f PixelPos) const {
    const Vec2 Size = getViewSizeM();
    const float Left = Center.X - Size.X * 0.5f;
    const float Top = Center.Y + Size.Y * 0.5f;
    const float Ppm = getPixelsPerMeter();
    return {Left + PixelPos.x / Ppm, Top - PixelPos.y / Ppm};
}

sf::View Camera::getWorldView() const {
    const Vec2 Size = getViewSizeM();
    return sf::View(toDraw(Center), {Size.X, Size.Y});
}

sf::View Camera::getScreenView() const {
    const sf::Vector2f Size{static_cast<float>(WindowPx.x), static_cast<float>(WindowPx.y)};
    return sf::View(sf::FloatRect({0.0f, 0.0f}, Size));
}

} // namespace fighter::render
