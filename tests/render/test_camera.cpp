#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "render/camera.hpp"

using fighter::Vec2;
using fighter::render::Camera;
using Catch::Approx;

TEST_CASE("Camera: world Y is up, screen Y is down", "[render][camera]") {
    Camera Cam({.ViewHeightM = 6.0f, .CenterM = {0.0f, 2.5f}});
    Cam.setWindowSize({1200, 600});   // 100 px/m, a 12 x 6 m view

    CHECK(Cam.getPixelsPerMeter() == Approx(100.0f));

    const auto CenterPx = Cam.worldToPixel({0.0f, 2.5f});
    CHECK(CenterPx.x == Approx(600.0f));
    CHECK(CenterPx.y == Approx(300.0f));

    const auto FloorPx = Cam.worldToPixel({0.0f, 0.0f});
    CHECK(FloorPx.y == Approx(550.0f));   // the floor is 0.5 m above the bottom edge

    const auto HigherPx = Cam.worldToPixel({0.0f, 1.0f});
    CHECK(HigherPx.y < FloorPx.y);        // higher in the world is higher on screen
}

TEST_CASE("Camera: forward and inverse transforms agree", "[render][camera]") {
    Camera Cam;
    Cam.setWindowSize({1280, 720});
    for (const Vec2 WorldPos : {Vec2{0, 0}, Vec2{-3.5f, 1.2f}, Vec2{4.0f, 5.0f}}) {
        const Vec2 Back = Cam.pixelToWorld(Cam.worldToPixel(WorldPos));
        CHECK(Back.X == Approx(WorldPos.X).margin(1e-4));
        CHECK(Back.Y == Approx(WorldPos.Y).margin(1e-4));
    }
}

TEST_CASE("Camera: worldView matches worldToPixel", "[render][camera]") {
    Camera Cam;
    Cam.setWindowSize({1280, 720});
    const sf::View View = Cam.getWorldView();
    const Vec2 WorldPos{2.0f, 1.5f};

    // Normalized coordinates of the SFML view -> window pixels.
    const sf::Vector2f Ndc = View.getTransform().transformPoint(Camera::toDraw(WorldPos));
    const sf::Vector2f ViaView{(Ndc.x + 1.0f) * 0.5f * 1280.0f, (1.0f - Ndc.y) * 0.5f * 720.0f};
    const sf::Vector2f Direct = Cam.worldToPixel(WorldPos);
    CHECK(ViaView.x == Approx(Direct.x).margin(0.01));
    CHECK(ViaView.y == Approx(Direct.y).margin(0.01));
}

TEST_CASE("Camera: view height in meters survives aspect change", "[render][camera]") {
    Camera Cam({.ViewHeightM = 6.0f, .CenterM = {0.0f, 2.5f}});
    Cam.setWindowSize({800, 800});
    CHECK(Cam.getViewSizeM().Y == Approx(6.0f));
    CHECK(Cam.getViewSizeM().X == Approx(6.0f));
    Cam.setWindowSize({1600, 800});
    CHECK(Cam.getViewSizeM().X == Approx(12.0f));
}
