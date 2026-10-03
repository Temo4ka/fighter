#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

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
