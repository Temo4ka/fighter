#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "render/scene.hpp"

using namespace fighter;
using namespace fighter::render;

namespace {

/// A fighter whose parts are told apart by their position: part I at x = I.
combat::FighterView makeFighter(float OffsetX, bool FacingRight) {
    combat::FighterView Fighter;
    Fighter.FacingRight = FacingRight;
    Fighter.Hp = 50.0f;
    Fighter.MaxHp = 100.0f;
    Fighter.Stamina = 30.0f;
    Fighter.MaxStamina = 40.0f;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        Fighter.Parts.push_back({.Part = static_cast<BodyPart>(Index),
                                 .Position = {OffsetX + static_cast<float>(Index), 1.0f},
                                 .Angle = 0.3f,
                                 .Size = {0.1f, 0.2f + 0.01f * static_cast<float>(Index)}});
    }
    return Fighter;
}

combat::RenderSnapshot makeSnapshot() {
    combat::RenderSnapshot Snapshot;
    Snapshot.TimeLeftSec = 89.2;
    Snapshot.Fighters = {makeFighter(0.0f, true), makeFighter(100.0f, false)};
    return Snapshot;
}

/// The body part a world primitive stands for, from its position.
std::optional<BodyPart> findPart(const Primitive& What) {
    float X = -1.0f;
    if (const auto* Sprite = std::get_if<SpritePrim>(&What)) X = Sprite->Position.X;
    if (const auto* Capsule = std::get_if<CapsulePrim>(&What)) X = Capsule->Position.X;
    if (X < 0.0f) return std::nullopt;
    const auto Index = static_cast<size_t>(X) % 100;
    return Index < BodyPartCount ? std::optional(static_cast<BodyPart>(Index)) : std::nullopt;
}

std::vector<const RenderItem*> getLayer(const std::vector<RenderItem>& Items, Layer Where) {
    std::vector<const RenderItem*> Out;
    for (const RenderItem& Item : Items) {
        if (Item.Where == Where) Out.push_back(&Item);
    }
    return Out;
}

} // namespace

TEST_CASE("Scene: parts without pictures are capsules of the part size", "[render][scene]") {
    const combat::RenderSnapshot Snapshot = makeSnapshot();
    const RenderList List = buildRenderList(Snapshot, Visuals{}, SceneInput{});

    CHECK(List.getStats().Fallbacks == 2 * BodyPartCount);
    CHECK(List.getStats().Sprites == 0);
    for (const RenderItem* Item : getLayer(List.getItems(), Layer::NearFighter)) {
        const auto& Capsule = std::get<CapsulePrim>(Item->What);
        const PartTransform& Source = Snapshot.Fighters[0].Parts[static_cast<size_t>(*findPart(Item->What))];
        CHECK(Capsule.Size.X == Source.Size.X);
        CHECK(Capsule.Size.Y == Source.Size.Y);
        CHECK(Capsule.Angle == Source.Angle);
    }
}

TEST_CASE("Scene: parts are drawn in the order of docs/ART.md", "[render][scene]") {
    const RenderList List = buildRenderList(makeSnapshot(), Visuals{}, SceneInput{});
    const std::vector<RenderItem> Sorted = List.getSorted();
    std::vector<BodyPart> Order;
    for (const RenderItem* Item : getLayer(Sorted, Layer::NearFighter)) Order.push_back(*findPart(Item->What));

    const auto Expected = getPartDrawOrder();
    CHECK(Order == std::vector<BodyPart>(Expected.begin(), Expected.end()));
    CHECK(Order.front() == BodyPart::UpperArmR);
    CHECK(Order.back() == BodyPart::ForearmL);
}

TEST_CASE("Scene: the far fighter goes on the far layer", "[render][scene]") {
    SceneInput Scene;
    Scene.FarFighter = 0;
    const RenderList List = buildRenderList(makeSnapshot(), Visuals{}, Scene);
    for (const RenderItem* Item : getLayer(List.getItems(), Layer::FarFighter))
        CHECK(std::get<CapsulePrim>(Item->What).Position.X < 100.0f);
    for (const RenderItem* Item : getLayer(List.getItems(), Layer::NearFighter))
        CHECK(std::get<CapsulePrim>(Item->What).Position.X >= 100.0f);

    // The layers come out in order: background, arena, far, near, effects, HUD.
    const std::vector<RenderItem> Sorted = List.getSorted();
    for (size_t Index = 1; Index < Sorted.size(); ++Index) CHECK(Sorted[Index - 1].Where <= Sorted[Index].Where);
}

TEST_CASE("Scene: a fighter facing left has mirrored pictures", "[render][scene]") {
    const sf::Texture Picture;
    FighterSprites Sprites;
    for (SpriteRef& Part : Sprites.Parts) Part = {.Texture = &Picture, .MetersPerPixel = 0.02f};

    SceneInput Scene;
    Scene.Sprites = {&Sprites, &Sprites};
    const RenderList List = buildRenderList(makeSnapshot(), Visuals{}, Scene);
    CHECK(List.getStats().Sprites == 2 * BodyPartCount);
    CHECK(List.getStats().Fallbacks == 0);

    for (const RenderItem& Item : List.getItems()) {
        const auto* Sprite = std::get_if<SpritePrim>(&Item.What);
        if (!Sprite || Sprite->Texture != &Picture) continue;
        const bool Right = Sprite->Position.X >= 100.0f;   // the right fighter faces left
        CHECK(Sprite->Scale.X == (Right ? -0.02f : 0.02f));
        CHECK(Sprite->Scale.Y == 0.02f);
        CHECK(Sprite->Angle == 0.3f);
    }
}

TEST_CASE("Scene: equipment goes right after its part", "[render][scene]") {
    const sf::Texture Body;
    const sf::Texture Weapon;
    FighterSprites Sprites;
    for (SpriteRef& Part : Sprites.Parts) Part = {.Texture = &Body};
    Sprites.Overlays[static_cast<size_t>(BodyPart::ForearmR)].push_back(
        {.Texture = &Weapon, .MetersPerPixel = 0.01f, .Origin = {0.5f, 0.2f}});

    SceneInput Scene;
    Scene.Sprites = {&Sprites, nullptr};
    const RenderList List = buildRenderList(makeSnapshot(), Visuals{}, Scene);
    const std::vector<RenderItem> Sorted = List.getSorted();
    const auto Near = getLayer(Sorted, Layer::NearFighter);
    REQUIRE(Near.size() == BodyPartCount + 1);

    // UpperArmR, ForearmR, the weapon, then the rest.
    const auto& Overlay = std::get<SpritePrim>(Near[2]->What);
    CHECK(Overlay.Texture == &Weapon);
    CHECK(Overlay.Origin.Y == 0.2f);
    CHECK(Overlay.Position.X == static_cast<float>(BodyPart::ForearmR));
    CHECK(*findPart(Near[1]->What) == BodyPart::ForearmR);
    CHECK(*findPart(Near[3]->What) == BodyPart::ThighR);
}

TEST_CASE("Scene: the HUD has bars, names and the timer", "[render][scene]") {
    SceneInput Scene;
    Scene.Names = {"Knight", "Rogue"};
    Scene.ScreenSizePx = {1000.0f, 600.0f};
    Visuals Vis;
    Vis.Hud.BarWidthPx = 300.0f;
    Vis.Hud.MarginPx = 20.0f;
    const RenderList List = buildRenderList(makeSnapshot(), Vis, Scene);

    std::vector<BarPrim> Bars;
    std::vector<TextPrim> Texts;
    for (const RenderItem* Item : getLayer(List.getItems(), Layer::Hud)) {
        if (const auto* Bar = std::get_if<BarPrim>(&Item->What)) Bars.push_back(*Bar);
        if (const auto* Text = std::get_if<TextPrim>(&Item->What)) Texts.push_back(*Text);
    }
    REQUIRE(Bars.size() == 4);   // HP and stamina of both
    CHECK(Bars[0].Ratio == 0.5f);
    CHECK(Bars[1].Ratio == 0.75f);
    CHECK(Bars[0].Position.X == 20.0f);
    CHECK_FALSE(Bars[0].FillFromRight);
    CHECK(Bars[2].Position.X == 1000.0f - 20.0f - 300.0f);
    CHECK(Bars[2].FillFromRight);

    REQUIRE(Texts.size() == 3);
    CHECK(Texts[0].Text == "Knight");
    CHECK(Texts[1].Text == "Rogue");
    CHECK(Texts[1].AlignX == 1.0f);
    CHECK(Texts[2].Text == "90");   // 89.2 s left, rounded up
    CHECK(Texts[2].Position.X == 500.0f);
}

TEST_CASE("Scene: no stamina bar when its height is zero", "[render][scene]") {
    Visuals Vis;
    Vis.Hud.StaminaBarHeightPx = 0.0f;
    const RenderList List = buildRenderList(makeSnapshot(), Vis, SceneInput{});
    size_t Bars = 0;
    for (const RenderItem& Item : List.getItems()) Bars += std::holds_alternative<BarPrim>(Item.What) ? 1 : 0;
    CHECK(Bars == 2);
}

TEST_CASE("Scene: a fighter without parts is one rectangle", "[render][scene]") {
    combat::RenderSnapshot Snapshot;
    const RenderList List = buildRenderList(Snapshot, Visuals{}, SceneInput{});
    CHECK(getLayer(List.getItems(), Layer::NearFighter).size() == 1);
    CHECK(getLayer(List.getItems(), Layer::FarFighter).size() == 1);
    CHECK(List.getStats().Fallbacks == 0);
}
