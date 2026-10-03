#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "render/sprites.hpp"

using namespace fighter;
using namespace fighter::render;

namespace {

/// Pretends that exactly the files in Present exist.
struct FakeFiles {
    std::set<std::string> Present;
    std::set<std::string> Asked;
    sf::Texture Picture;

    TextureLoader getLoader() {
        return [this](const std::string& Path, bool) -> const sf::Texture* {
            Asked.insert(Path);
            return Present.contains(Path) ? &Picture : nullptr;
        };
    }
};

Visuals makeVisuals() {
    Visuals Vis;
    Vis.DefaultSkin = "base";
    Vis.Skins["base"] = SkinDef{.Dir = "skins/base", .ItemsDir = "items", .PixelsPerMeter = 50.0f};
    Vis.Skins["other"] = SkinDef{.Dir = "skins/other", .PixelsPerMeter = 25.0f};
    return Vis;
}

} // namespace

TEST_CASE("Sprites: picture path is dir/Part.png", "[render][sprites]") {
    CHECK(getPicturePath("assets/a", BodyPart::ForearmR) == "assets/a/ForearmR.png");
}

TEST_CASE("Sprites: makeFighterLook takes the name and the equipment from the config", "[render][sprites]") {
    combat::FighterConfig Config;
    Config.Loadout.Items.push_back({.Id = "iron_helmet", .Covers = {BodyPart::Head}});
    const FighterLook Look = makeFighterLook(Config, "base", "P1");
    CHECK(Look.Name == "P1");   // no name in the config
    CHECK(Look.Skin == "base");
    REQUIRE(Look.Items.size() == 1);
    CHECK(Look.Items[0].Id == "iron_helmet");
    CHECK(Look.Items[0].Covers == std::vector<BodyPart>{BodyPart::Head});

    Config.Name = "Knight";
    CHECK(makeFighterLook(Config, "", "P1").Name == "Knight");
}

TEST_CASE("Sprites: found parts get pictures, missing ones fall back", "[render][sprites]") {
    FakeFiles Files;
    Files.Present = {"skins/base/Head.png", "skins/base/Torso.png"};
    const FighterSprites Sprites = resolveFighterSprites(makeVisuals(), {.Name = "P1"}, Files.getLoader());

    CHECK(Sprites.Parts[static_cast<size_t>(BodyPart::Head)].Texture == &Files.Picture);
    CHECK(Sprites.Parts[static_cast<size_t>(BodyPart::Torso)].MetersPerPixel == 1.0f / 50.0f);
    CHECK(Sprites.Parts[static_cast<size_t>(BodyPart::FootL)].Texture == nullptr);
    CHECK(Sprites.MissingFiles == BodyPartCount - 2);
    CHECK(Files.Asked.size() == BodyPartCount);
}

TEST_CASE("Sprites: the look's skin wins over the default, an unknown skin draws capsules", "[render][sprites]") {
    FakeFiles Files;
    Files.Present = {"skins/other/Head.png"};
    const FighterSprites Other = resolveFighterSprites(makeVisuals(), {.Skin = "other"}, Files.getLoader());
    CHECK(Other.Parts[static_cast<size_t>(BodyPart::Head)].Texture == &Files.Picture);
    CHECK(Other.Parts[static_cast<size_t>(BodyPart::Head)].MetersPerPixel == 1.0f / 25.0f);

    const FighterSprites Unknown = resolveFighterSprites(makeVisuals(), {.Skin = "nope"}, Files.getLoader());
    for (const SpriteRef& Part : Unknown.Parts) CHECK(Part.Texture == nullptr);
}

TEST_CASE("Sprites: item overlays come from the skin's items dir or the item's own dir", "[render][sprites]") {
    Visuals Vis = makeVisuals();
    Vis.Items["sword"] = ItemVisualDef{.Dir = "weapons/sword", .Origins = {{BodyPart::ForearmR, {0.5f, 0.2f}}}};

    FakeFiles Files;
    Files.Present = {"items/chainmail/Torso.png", "items/chainmail/UpperArmL.png", "weapons/sword/ForearmR.png"};
    FighterLook Look;
    Look.Items = {{"chainmail", {BodyPart::Torso, BodyPart::UpperArmL, BodyPart::UpperArmR}},
                  {"sword", {BodyPart::ForearmR}}};
    const FighterSprites Sprites = resolveFighterSprites(Vis, Look, Files.getLoader());

    CHECK(Sprites.Overlays[static_cast<size_t>(BodyPart::Torso)].size() == 1);
    CHECK(Sprites.Overlays[static_cast<size_t>(BodyPart::UpperArmL)].size() == 1);
    CHECK(Sprites.Overlays[static_cast<size_t>(BodyPart::UpperArmR)].empty());   // missing: not drawn
    REQUIRE(Sprites.Overlays[static_cast<size_t>(BodyPart::ForearmR)].size() == 1);
    const SpriteRef& Sword = Sprites.Overlays[static_cast<size_t>(BodyPart::ForearmR)][0];
    CHECK(Sword.Origin.Y == 0.2f);
    CHECK(Sword.MetersPerPixel == 1.0f / 50.0f);   // the skin's density
    // 13 skin parts and the chainmail's UpperArmR.
    CHECK(Sprites.MissingFiles == BodyPartCount + 1);
}

TEST_CASE("Sprites: an item without a configured dir is skipped quietly", "[render][sprites]") {
    FakeFiles Files;
    FighterLook Look;
    Look.Skin = "other";   // no items_dir
    Look.Items = {{"helmet", {BodyPart::Head}}};
    const FighterSprites Sprites = resolveFighterSprites(makeVisuals(), Look, Files.getLoader());
    CHECK(Sprites.Overlays[static_cast<size_t>(BodyPart::Head)].empty());
    CHECK(Sprites.MissingFiles == BodyPartCount);
}
