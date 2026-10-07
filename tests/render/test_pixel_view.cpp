#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "render/pixel_view.hpp"

using namespace fighter;
using namespace fighter::render;
using Catch::Approx;

TEST_CASE("PixelView: auto scale is the largest that fits the view height", "[render][pixel_view]") {
    // 6 m at 32 px/m = 192 low-resolution pixels; 720 / 192 = 3.75 -> 3.
    const PixelLayout Layout = computePixelLayout({1280, 720}, 6.0f, 32.0f, 0, true);
    CHECK(Layout.Scale == 3);
    CHECK(Layout.LowResSizePx.y == 192);
    CHECK(Layout.LowResSizePx.x == 426);
    // Centred: (720 - 576) / 2 bars above and below, (1280 - 1278) / 2 at the sides.
    CHECK(Layout.OffsetPx.y == 72);
    CHECK(Layout.OffsetPx.x == 1);

    // A window smaller than the view still gets scale 1.
    CHECK(computePixelLayout({300, 100}, 6.0f, 32.0f, 0, true).Scale == 1);
}

TEST_CASE("PixelView: a fixed scale is lowered to fit only with letterbox", "[render][pixel_view]") {
    CHECK(computePixelLayout({1280, 720}, 6.0f, 32.0f, 2, true).Scale == 2);
    CHECK(computePixelLayout({1280, 720}, 6.0f, 32.0f, 5, true).Scale == 3);

    // Without letterbox the picture covers the window, even past its edges.
    const PixelLayout Fill = computePixelLayout({1280, 720}, 6.0f, 32.0f, 5, false);
    CHECK(Fill.Scale == 5);
    CHECK(Fill.LowResSizePx.x == 256);
    CHECK(Fill.LowResSizePx.y == 144);
    CHECK(Fill.OffsetPx.x == 0);
    CHECK(Fill.OffsetPx.y == 0);
    const PixelLayout Odd = computePixelLayout({1281, 721}, 6.0f, 32.0f, 2, false);
    CHECK(Odd.LowResSizePx.x * Odd.Scale >= 1281);
    CHECK(Odd.LowResSizePx.y * Odd.Scale >= 721);
}

TEST_CASE("PixelView: snapping puts the view edges on the pixel grid", "[render][pixel_view]") {
    const float Ppm = 32.0f;
    for (const Vec2 Center : {Vec2{0.0f, 2.5f}, Vec2{0.013f, 2.4991f}, Vec2{-1.37f, 3.02f}}) {
        for (const sf::Vector2u Size : {sf::Vector2u{426, 192}, sf::Vector2u{427, 193}}) {
            const Vec2 Snapped = snapToPixelGrid(Center, Size, Ppm);
            const float Left = (Snapped.X - static_cast<float>(Size.x) * 0.5f / Ppm) * Ppm;
            const float Top = (Snapped.Y + static_cast<float>(Size.y) * 0.5f / Ppm) * Ppm;
            CHECK(Left == Approx(std::round(Left)).margin(1e-3));
            CHECK(Top == Approx(std::round(Top)).margin(1e-3));
            // Never more than half a pixel away.
            CHECK(std::abs(Snapped.X - Center.X) <= 0.5f / Ppm + 1e-5f);
            CHECK(std::abs(Snapped.Y - Center.Y) <= 0.5f / Ppm + 1e-5f);
        }
    }
}

TEST_CASE("PixelView: the display camera shows the world where the picture does", "[render][pixel_view]") {
    const sf::Vector2u Window{1280, 720};
    const PixelLayout Layout = computePixelLayout(Window, 6.0f, 32.0f, 0, true);
    const Camera LowRes = makeLowResCamera(Layout, {0.21f, 2.5f});
    CHECK(LowRes.getPixelsPerMeter() == Approx(32.0f));
    CHECK(LowRes.getWindowSize() == Layout.LowResSizePx);

    const Camera Display = makeDisplayCamera(Layout, LowRes, Window);
    CHECK(Display.getPixelsPerMeter() == Approx(96.0f));
    for (const Vec2 Point : {Vec2{0.0f, 0.0f}, Vec2{1.5f, 1.8f}, Vec2{-2.0f, 4.0f}}) {
        const sf::Vector2f Low = LowRes.worldToPixel(Point);
        const sf::Vector2f High = Display.worldToPixel(Point);
        const auto Scale = static_cast<float>(Layout.Scale);
        CHECK(High.x == Approx(Low.x * Scale + static_cast<float>(Layout.OffsetPx.x)).margin(1e-2));
        CHECK(High.y == Approx(Low.y * Scale + static_cast<float>(Layout.OffsetPx.y)).margin(1e-2));
    }
}
