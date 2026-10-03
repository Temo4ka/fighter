#include "render/battle_renderer.hpp"

#include <exception>
#include <format>
#include <ranges>
#include <string>
#include <utility>

#include "core/log.hpp"
#include "debug/draw.hpp"
#include "render/assets.hpp"
#include "render/scene.hpp"

namespace fighter::render {

BattleRenderer::BattleRenderer(Resources& Res, double StepSec) : Assets(Res), Effects(StepSec) {
    Looks = {FighterLook{.Name = "P1"}, FighterLook{.Name = "P2"}};
    // The pictures are looked up when the fight starts (startBattle).
    if (!loadVisualsFile()) log::warn("drawing with the default visuals");
}

bool BattleRenderer::reloadVisuals() {
    if (!loadVisualsFile()) return false;
    // Pictures may have changed on disk as well.
    Frame = {};
    Assets.clearTextures();
    resolveSprites();
    return true;
}

void BattleRenderer::startBattle(std::array<FighterLook, 2> NewLooks) {
    Looks = std::move(NewLooks);
    Effects.clear();
    resolveSprites();
}

void BattleRenderer::onBattleEvent(const combat::BattleEvent& Event, const combat::RenderSnapshot& After) {
    Effects.onEvent(Event, After, Vis.Effects);
}

void BattleRenderer::buildFrame(const Camera& Cam, const combat::RenderSnapshot& Snapshot, float Alpha) {
    const double FrameTick = getFrameTick(Snapshot.Tick, Alpha);
    FrameCam = Cam;
    FrameCam.setCenterM(Cam.getCenterM() + Effects.getCameraOffset(FrameTick, Vis.Effects));

    SceneInput Scene;
    Scene.Sprites = {&Sprites[0], &Sprites[1]};
    Scene.Names = {Looks[0].Name, Looks[1].Name};
    Scene.FarFighter = Effects.getFarFighter();
    Scene.Background = Vis.Background.empty() ? nullptr : Assets.findTexture(Vis.Background);
    Scene.ViewCenterM = FrameCam.getCenterM();
    Scene.ViewSizeM = FrameCam.getViewSizeM();
    Scene.ScreenSizePx = {static_cast<float>(FrameCam.getWindowSize().x),
                          static_cast<float>(FrameCam.getWindowSize().y)};
    Scene.Effects = &Effects;
    Scene.FrameTick = FrameTick;
    Frame = buildRenderList(Snapshot, Vis, Scene);

    if constexpr (FIGHTER_DEBUG) {
        const RenderStats Stats = Frame.getStats();
        debug::setPanel("render", std::format("{} prims, {} sprites, {} fallbacks, {} files missing", Stats.Primitives,
                                              Stats.Sprites, Stats.Fallbacks,
                                              Sprites[0].MissingFiles + Sprites[1].MissingFiles));
        debug::setPanel("render fx", std::format("{} active, far P{}", Effects.getActiveCount(FrameTick, Vis.Effects),
                                                 Scene.FarFighter + 1));
    }
}

void BattleRenderer::drawWorld(sf::RenderTarget& Target) const {
    drawRenderList(Target, FrameCam, Frame, Layer::Background, Layer::Effects, Assets.getFont(assets::MonoFontPath));
}

void BattleRenderer::drawHud(sf::RenderTarget& Target) const {
    drawRenderList(Target, FrameCam, Frame, Layer::Hud, Layer::Hud, Assets.getFont(assets::MonoFontPath));
}

bool BattleRenderer::loadVisualsFile() {
    try {
        Vis = loadVisuals(Assets.getRoot() / assets::VisualsPath);
    } catch (const std::exception& Error) {
        log::error("cannot load visuals: {}", Error.what());
        debug::logEvent(std::format("visuals reload failed: {}", Error.what()));
        return false;
    }
    return true;
}

void BattleRenderer::resolveSprites() {
    const TextureLoader Load = [this](const std::string& Path, bool Smooth) { return Assets.findTexture(Path, Smooth); };
    for (auto&& [Look, Pictures] : std::views::zip(Looks, Sprites)) Pictures = resolveFighterSprites(Vis, Look, Load);
}

} // namespace fighter::render
