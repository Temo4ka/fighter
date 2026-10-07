#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <variant>

#include "render/battle_renderer.hpp"

using namespace fighter;
using namespace fighter::render;

namespace {

combat::RenderSnapshot makeSnapshot(uint64_t Tick) {
    combat::RenderSnapshot Snapshot;
    Snapshot.Tick = Tick;
    Snapshot.TimeLeftSec = 90.0;
    for (auto& Fighter : Snapshot.Fighters) {
        for (size_t Index = 0; Index < BodyPartCount; ++Index)
            Fighter.Parts.push_back({.Part = static_cast<BodyPart>(Index), .Position = {0.0f, 1.0f}, .Size = {0.1f, 0.2f}});
    }
    return Snapshot;
}

size_t countLayer(const RenderList& List, Layer Where) {
    size_t Count = 0;
    for (const RenderItem& Item : List.getItems()) Count += Item.Where == Where ? 1 : 0;
    return Count;
}

} // namespace

TEST_CASE("Resources: findTexture returns null for a missing file", "[render][resources]") {
    Resources Res(FIGHTER_SOURCE_DIR);
    CHECK(Res.findTexture("assets/no/such/picture.png") == nullptr);
    CHECK(Res.findTexture("assets/no/such/picture.png") == nullptr);   // remembered, no exception
    Res.clearTextures();
    CHECK(Res.findTexture("assets/no/such/picture.png") == nullptr);
}

TEST_CASE("BattleRenderer: a frame has every fighter part, effects and the HUD", "[render][battle_renderer]") {
    Resources Res(FIGHTER_SOURCE_DIR);
    BattleRenderer Renderer(Res, 1.0 / 60.0);
    REQUIRE(Renderer.reloadVisuals());
    Renderer.startBattle({FighterLook{.Name = "A"}, FighterLook{.Name = "B"}});

    Camera Cam;
    Renderer.buildFrame(Cam, makeSnapshot(10), 0.5f);
    const RenderStats Stats = Renderer.getFrame().getStats();
    // Each part is either a picture or a capsule, whatever is on disk.
    CHECK(countLayer(Renderer.getFrame(), Layer::FarFighter) == BodyPartCount);
    CHECK(countLayer(Renderer.getFrame(), Layer::NearFighter) == BodyPartCount);
    CHECK(Stats.Fallbacks <= 2 * BodyPartCount);
    CHECK(countLayer(Renderer.getFrame(), Layer::Effects) == 0);
    CHECK(countLayer(Renderer.getFrame(), Layer::Hud) > 0);

    // A fall starts the dust, a hit on the left fighter puts it behind.
    combat::StrikeLanded Hit;
    Hit.Contact.Victim.Fighter = 0;
    Renderer.onBattleEvent(Hit, makeSnapshot(11));
    Renderer.onBattleEvent(combat::KnockedDown{.Fighter = 0}, makeSnapshot(11));
    Renderer.buildFrame(Cam, makeSnapshot(11), 0.5f);
    CHECK(countLayer(Renderer.getFrame(), Layer::Effects) ==
          static_cast<size_t>(Renderer.getVisuals().Effects.Dust.Particles));

    // A new fight forgets the effects.
    Renderer.startBattle({FighterLook{}, FighterLook{}});
    Renderer.buildFrame(Cam, makeSnapshot(11), 0.5f);
    CHECK(countLayer(Renderer.getFrame(), Layer::Effects) == 0);
}

TEST_CASE("BattleRenderer: styles switch the skin and the pixel mode", "[render][battle_renderer]") {
    Resources Res(FIGHTER_SOURCE_DIR);
    BattleRenderer Renderer(Res, 1.0 / 60.0);
    const Visuals& Vis = Renderer.getVisuals();
    REQUIRE(Vis.Styles.contains("pixel"));
    REQUIRE(Vis.Styles.contains("smooth"));
    CHECK(Renderer.getStyleName() == Vis.Style);
    Renderer.startBattle({FighterLook{.Name = "A"}, FighterLook{.Name = "B"}});

    Camera Cam;
    Cam.setWindowSize({1280, 720});
    REQUIRE(Renderer.selectStyle("smooth"));
    Renderer.buildFrame(Cam, makeSnapshot(10), 0.0f);
    CHECK_FALSE(Renderer.getPixelLayout());
    CHECK(Renderer.getOverlayCamera().getPixelsPerMeter() == Cam.getPixelsPerMeter());

    REQUIRE(Renderer.selectStyle("pixel"));
    Renderer.buildFrame(Cam, makeSnapshot(10), 0.0f);
    REQUIRE(Renderer.getPixelLayout());
    const PixelLayout& Layout = *Renderer.getPixelLayout();
    CHECK(Layout.Scale >= 1);
    CHECK(Renderer.getOverlayCamera().getWindowSize() == Cam.getWindowSize());
    CHECK(Renderer.getOverlayCamera().getPixelsPerMeter() ==
          static_cast<float>(Layout.Scale) * Layout.PixelsPerMeter);
    // The HUD is laid out for the window, not for the low-resolution picture.
    float RightmostBar = 0.0f;
    for (const RenderItem& Item : Renderer.getFrame().getItems()) {
        if (const auto* Bar = std::get_if<BarPrim>(&Item.What); Bar && Item.Where == Layer::Hud)
            RightmostBar = std::max(RightmostBar, Bar->Position.X);
    }
    CHECK(RightmostBar > static_cast<float>(Layout.LowResSizePx.x));

    // The key's choice survives F5; an unknown name changes nothing.
    REQUIRE(Renderer.reloadVisuals());
    CHECK(Renderer.getStyleName() == "pixel");
    CHECK_FALSE(Renderer.selectStyle("no_such_style"));
    CHECK(Renderer.getStyleName() == "pixel");
    CHECK(Renderer.cycleStyle() == "smooth");
    CHECK(Renderer.cycleStyle() == "pixel");
}
