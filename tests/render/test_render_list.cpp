#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <variant>
#include <vector>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#include "render/render_list.hpp"

using namespace fighter;
using namespace fighter::render;

TEST_CASE("RenderList: sorted by layer, in insertion order within a layer", "[render][render_list]") {
    RenderList List;
    List.add(Layer::Hud, RectPrim{.Position = {1.0f, 0.0f}});
    List.add(Layer::NearFighter, RectPrim{.Position = {2.0f, 0.0f}});
    List.add(Layer::Background, RectPrim{.Position = {3.0f, 0.0f}});
    List.add(Layer::NearFighter, RectPrim{.Position = {4.0f, 0.0f}});
    List.add(Layer::FarFighter, RectPrim{.Position = {5.0f, 0.0f}});

    std::vector<float> Order;
    for (const RenderItem& Item : List.getSorted()) Order.push_back(std::get<RectPrim>(Item.What).Position.X);
    CHECK(Order == std::vector<float>{3.0f, 5.0f, 2.0f, 4.0f, 1.0f});
    // The list itself keeps the insertion order.
    CHECK(List.getItems().front().Where == Layer::Hud);
}

TEST_CASE("RenderList: drawRenderList draws only the requested layers in order", "[render][render_list]") {
    sf::RenderTexture Target;
    if (!Target.resize({40, 40})) SKIP("no graphics context for an offscreen texture");
    const sf::Font Font(std::filesystem::path(FIGHTER_SOURCE_DIR) / "assets/fonts/JetBrainsMono-Regular.ttf");
    Camera Cam;
    Cam.setWindowSize({40, 40});

    RenderList List;
    // Two squares at the same place on the Hud layer: the later one wins.
    List.add(Layer::Hud, RectPrim{.Position = {20.0f, 20.0f}, .Size = {20.0f, 20.0f}, .Fill = sf::Color::Red});
    List.add(Layer::Hud, RectPrim{.Position = {20.0f, 20.0f}, .Size = {10.0f, 10.0f}, .Fill = sf::Color::Green});
    // A world square covering everything, but on a layer that is not drawn.
    List.add(Layer::Background, RectPrim{.Position = Cam.getCenterM(), .Size = {100.0f, 100.0f},
                                         .Fill = sf::Color::Blue});

    Target.clear(sf::Color::Black);
    drawRenderList(Target, Cam, List, Layer::Hud, Layer::Hud, Font);
    Target.display();
    const sf::Image Image = Target.getTexture().copyToImage();
    CHECK(Image.getPixel({20, 20}) == sf::Color::Green);
    CHECK(Image.getPixel({12, 12}) == sf::Color::Red);
    CHECK(Image.getPixel({2, 2}) == sf::Color::Black);
}

TEST_CASE("RenderList: stats count sprites and fallbacks", "[render][render_list]") {
    const sf::Texture Picture;
    RenderList List;
    List.add(Layer::NearFighter, SpritePrim{.Texture = &Picture});
    List.add(Layer::NearFighter, SpritePrim{.Texture = &Picture});
    List.addFallback(Layer::NearFighter, CapsulePrim{.Size = {0.1f, 0.3f}});
    List.add(Layer::Effects, CirclePrim{.Radius = 0.1f});

    const RenderStats Stats = List.getStats();
    CHECK(Stats.Primitives == 4);
    CHECK(Stats.Sprites == 2);
    CHECK(Stats.Fallbacks == 1);
}
