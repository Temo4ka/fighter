#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "render/camera.hpp"

using fighter::Vec2;
using fighter::render::Camera;
using Catch::Approx;

TEST_CASE("Camera: world Y is up, screen Y is down", "[render][camera]") {
    Camera cam({.viewHeightM = 6.0f, .centerM = {0.0f, 2.5f}});
    cam.setWindowSize({1200, 600});   // 100 px/м, вид 12 × 6 м

    CHECK(cam.pixelsPerMeter() == Approx(100.0f));

    const auto center = cam.worldToPixel({0.0f, 2.5f});
    CHECK(center.x == Approx(600.0f));
    CHECK(center.y == Approx(300.0f));

    const auto floorPx = cam.worldToPixel({0.0f, 0.0f});
    CHECK(floorPx.y == Approx(550.0f));   // пол на 0.5 м выше нижнего края

    const auto higher = cam.worldToPixel({0.0f, 1.0f});
    CHECK(higher.y < floorPx.y);          // выше в мире — выше на экране
}

TEST_CASE("Camera: forward and inverse transforms agree", "[render][camera]") {
    Camera cam;
    cam.setWindowSize({1280, 720});
    for (const Vec2 w : {Vec2{0, 0}, Vec2{-3.5f, 1.2f}, Vec2{4.0f, 5.0f}}) {
        const Vec2 back = cam.pixelToWorld(cam.worldToPixel(w));
        CHECK(back.x == Approx(w.x).margin(1e-4));
        CHECK(back.y == Approx(w.y).margin(1e-4));
    }
}

TEST_CASE("Camera: worldView matches worldToPixel", "[render][camera]") {
    Camera cam;
    cam.setWindowSize({1280, 720});
    const sf::View view = cam.worldView();
    const Vec2 w{2.0f, 1.5f};

    // Нормализованные координаты в виде SFML → пиксели окна.
    const sf::Vector2f ndc = view.getTransform().transformPoint(Camera::toDraw(w));
    const sf::Vector2f viaView{(ndc.x + 1.0f) * 0.5f * 1280.0f, (1.0f - ndc.y) * 0.5f * 720.0f};
    const sf::Vector2f direct = cam.worldToPixel(w);
    CHECK(viaView.x == Approx(direct.x).margin(0.01));
    CHECK(viaView.y == Approx(direct.y).margin(0.01));
}

TEST_CASE("Camera: view height in meters survives aspect change", "[render][camera]") {
    Camera cam({.viewHeightM = 6.0f, .centerM = {0.0f, 2.5f}});
    cam.setWindowSize({800, 800});
    CHECK(cam.viewSizeM().y == Approx(6.0f));
    CHECK(cam.viewSizeM().x == Approx(6.0f));
    cam.setWindowSize({1600, 800});
    CHECK(cam.viewSizeM().x == Approx(12.0f));
}
