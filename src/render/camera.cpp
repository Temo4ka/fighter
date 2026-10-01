#include "render/camera.hpp"

#include <algorithm>

namespace fighter::render {

Camera::Camera(Config config) : viewHeightM_(config.viewHeightM), center_(config.centerM) {}

void Camera::setWindowSize(sf::Vector2u sizePx) {
    windowPx_ = {std::max(sizePx.x, 1u), std::max(sizePx.y, 1u)};
}

float Camera::aspect() const {
    return static_cast<float>(windowPx_.x) / static_cast<float>(windowPx_.y);
}

float Camera::pixelsPerMeter() const {
    return static_cast<float>(windowPx_.y) / viewHeightM_;
}

sf::Vector2f Camera::worldToPixel(Vec2 world) const {
    const Vec2 size = viewSizeM();
    const float left = center_.x - size.x * 0.5f;
    const float top = center_.y + size.y * 0.5f;
    const float ppm = pixelsPerMeter();
    return {(world.x - left) * ppm, (top - world.y) * ppm};
}

Vec2 Camera::pixelToWorld(sf::Vector2f pixel) const {
    const Vec2 size = viewSizeM();
    const float left = center_.x - size.x * 0.5f;
    const float top = center_.y + size.y * 0.5f;
    const float ppm = pixelsPerMeter();
    return {left + pixel.x / ppm, top - pixel.y / ppm};
}

sf::View Camera::worldView() const {
    const Vec2 size = viewSizeM();
    return sf::View(toDraw(center_), {size.x, size.y});
}

sf::View Camera::screenView() const {
    const sf::Vector2f size{static_cast<float>(windowPx_.x), static_cast<float>(windowPx_.y)};
    return sf::View(sf::FloatRect({0.0f, 0.0f}, size));
}

} // namespace fighter::render
