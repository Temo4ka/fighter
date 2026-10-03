#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

#include "render/visuals.hpp"

using namespace fighter;
using namespace fighter::render;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

TEST_CASE("Visuals: the sample file loads", "[render][visuals]") {
    const Visuals Vis = loadVisuals(std::filesystem::path(FIGHTER_DATA_DIR) / "visuals.json");
    CHECK(Vis.PixelsPerMeter == 64.0f);
    REQUIRE(Vis.Skins.contains(Vis.DefaultSkin));
    REQUIRE(Vis.Skins.contains("placeholder_pixel"));
    CHECK(Vis.Skins.at("placeholder_pixel").PixelsPerMeter == 32.0f);
    CHECK_FALSE(Vis.Skins.at("placeholder_pixel").Smooth);
    CHECK(Vis.Effects.HitFlash.DurationSec > 0.0f);
}

TEST_CASE("Visuals: every key is optional and has a default", "[render][visuals]") {
    const Visuals Vis = parseVisuals("{}");
    CHECK(Vis.PixelsPerMeter == 64.0f);
    CHECK(Vis.DefaultSkin.empty());
    CHECK(Vis.Skins.empty());
    CHECK(Vis.Effects.CameraShake.MinReaction == combat::ReactionLevel::Knockback);
    CHECK(Vis.Hud.BarWidthPx == 360.0f);
}

TEST_CASE("Visuals: a skin inherits the density, an item may override it", "[render][visuals]") {
    const Visuals Vis = parseVisuals(R"({
        "pixels_per_meter": 48,
        "skins": { "a": { "dir": "art/a", "items_dir": "art/items" } },
        "items": { "sword": { "dir": "art/sword", "pixels_per_meter": 96, "origins": { "ForearmR": [0.5, 0.2] } } }
    })");
    CHECK(Vis.Skins.at("a").PixelsPerMeter == 48.0f);
    CHECK(Vis.Skins.at("a").ItemsDir == "art/items");
    CHECK(Vis.Skins.at("a").Smooth);
    const ItemVisualDef& Sword = Vis.Items.at("sword");
    CHECK(Sword.Dir == "art/sword");
    CHECK(Sword.PixelsPerMeter == 96.0f);
    REQUIRE(Sword.Origins.contains(BodyPart::ForearmR));
    CHECK(Sword.Origins.at(BodyPart::ForearmR).Y == 0.2f);
}

TEST_CASE("Visuals: effects and hud read every field", "[render][visuals]") {
    const Visuals Vis = parseVisuals(R"({
        "effects": {
            "hit_flash": { "min_reaction": "None", "duration_sec": 0.1, "radius_m": 0.2 },
            "camera_shake": { "min_reaction": "Stagger", "amplitude_m": 0.05, "duration_sec": 0.2, "frequency_hz": 20 },
            "dust": { "on_knockdown": false, "particles": 4, "duration_sec": 0.3, "spread_m": 0.4, "size_m": 0.05 }
        },
        "hud": { "bar_width_px": 300, "hp_bar_height_px": 20, "stamina_bar_height_px": 0, "margin_px": 10,
                 "gap_px": 2, "name_font_px": 14, "timer_font_px": 30 }
    })");
    CHECK(Vis.Effects.HitFlash.MinReaction == combat::ReactionLevel::None);
    CHECK(Vis.Effects.HitFlash.RadiusM == 0.2f);
    CHECK(Vis.Effects.CameraShake.MinReaction == combat::ReactionLevel::Stagger);
    CHECK(Vis.Effects.CameraShake.FrequencyHz == 20.0f);
    CHECK_FALSE(Vis.Effects.Dust.OnKnockdown);
    CHECK(Vis.Effects.Dust.Particles == 4);
    CHECK(Vis.Hud.BarWidthPx == 300.0f);
    CHECK(Vis.Hud.StaminaBarHeightPx == 0.0f);
    CHECK(Vis.Hud.TimerFontPx == 30u);
}

TEST_CASE("Visuals: errors name the field and the value", "[render][visuals]") {
    CHECK_THROWS_WITH(parseVisuals(R"({"pixel_per_meter": 64})"), Equals("unknown field 'pixel_per_meter'"));
    CHECK_THROWS_WITH(parseVisuals(R"({"skins": {"a": {"dir": "x", "smoth": true}}})"),
                      Equals("unknown field 'skins.a.smoth'"));
    CHECK_THROWS_WITH(parseVisuals(R"({"skins": {"a": {}}})"), Equals("missing field 'skins.a.dir'"));
    CHECK_THROWS_WITH(parseVisuals(R"({"pixels_per_meter": 0})"),
                      ContainsSubstring("field 'pixels_per_meter': 0 is outside"));
    CHECK_THROWS_WITH(parseVisuals(R"({"default_skin": "nope"})"),
                      Equals("field 'default_skin': 'nope' is not one of 'skins'"));
    CHECK_THROWS_WITH(parseVisuals(R"({"effects": {"hit_flash": {"min_reaction": "Big"}}})"),
                      ContainsSubstring("field 'effects.hit_flash.min_reaction': 'Big' is not one of"));
    CHECK_THROWS_WITH(parseVisuals(R"({"effects": {"dust": {"particles": 2.5}}})"),
                      Equals("field 'effects.dust.particles': 2.5 is not a whole number"));
    CHECK_THROWS_WITH(parseVisuals(R"({"items": {"s": {"origins": {"Hand": [0.5, 0.5]}}}})"),
                      Equals("field 'items.s.origins.Hand': 'Hand' is not a body part"));
    CHECK_THROWS_WITH(parseVisuals(R"({"items": {"s": {"origins": {"Head": [0.5]}}}})"),
                      ContainsSubstring("is not a pair of numbers"));
    CHECK_THROWS_WITH(parseVisuals(R"({"hud": {"margin_px": "big"}})"),
                      Equals("field 'hud.margin_px': \"big\" is not a number"));
}

TEST_CASE("Visuals: a missing file names the file", "[render][visuals]") {
    CHECK_THROWS_WITH(loadVisuals("no/such/visuals.json"), ContainsSubstring("no/such/visuals.json"));
}
