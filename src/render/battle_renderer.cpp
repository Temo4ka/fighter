#include "render/battle_renderer.hpp"

#include <exception>
#include <format>
#include <ranges>
#include <string>
#include <utility>

#include <SFML/Graphics/Sprite.hpp>

#include "core/log.hpp"
#include "debug/draw.hpp"
#include "render/assets.hpp"
#include "render/scene.hpp"

namespace fighter::render {

BattleRenderer::BattleRenderer(Resources& Res, double StepSec) : Assets(Res), Effects(StepSec) {
    Looks = {FighterLook{.Name = "P1"}, FighterLook{.Name = "P2"}};
    // The pictures are looked up when the fight starts (startBattle).
    if (!loadVisualsFile()) log::warn("drawing with the default visuals");
    settleStyle();
}

bool BattleRenderer::reloadVisuals() {
    if (!loadVisualsFile()) return false;
    settleStyle();
    // Pictures may have changed on disk as well.
    Frame = {};
    Assets.clearTextures();
    resolveSprites();
    return true;
}

bool BattleRenderer::selectStyle(std::string_view Name) {
    if (!Vis.Styles.contains(Name)) return false;
    StyleName = Name;
    StyleOverridden = true;
    resolveSprites();
    debug::logEvent(std::format("style: {}", StyleName));
    return true;
}

const std::string& BattleRenderer::cycleStyle() {
    if (Vis.Styles.empty()) return StyleName;
    auto Next = Vis.Styles.upper_bound(StyleName);
    if (Next == Vis.Styles.end()) Next = Vis.Styles.begin();
    // Copy the key: selectStyle() assigns StyleName from it.
    selectStyle(std::string(Next->first));
    return StyleName;
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
    const Vec2 CenterM = Cam.getCenterM() + Effects.getCameraOffset(FrameTick, Vis.Effects);
    HudCam = Cam;
    if (const StyleDef& Style = getStyle(Vis, StyleName); Style.PixelArt) {
        const PixelArtParams& Params = *Style.PixelArt;
        Pixel = computePixelLayout(Cam.getWindowSize(), Cam.getViewSizeM().Y, getPixelArtDensity(Params),
                                   Params.Scale, Params.Letterbox);
        FrameCam = makeLowResCamera(*Pixel, CenterM);
        OverlayCam = makeDisplayCamera(*Pixel, FrameCam, Cam.getWindowSize());
    } else {
        Pixel.reset();
        FrameCam = Cam;
        FrameCam.setCenterM(CenterM);
        OverlayCam = FrameCam;
    }

    SceneInput Scene;
    Scene.Sprites = {&Sprites[0], &Sprites[1]};
    Scene.Names = {Looks[0].Name, Looks[1].Name};
    Scene.FarFighter = Effects.getFarFighter();
    Scene.Background = Vis.Background.empty() ? nullptr : Assets.findTexture(Vis.Background);
    Scene.ViewCenterM = FrameCam.getCenterM();
    Scene.ViewSizeM = FrameCam.getViewSizeM();
    Scene.ScreenSizePx = {static_cast<float>(HudCam.getWindowSize().x),
                          static_cast<float>(HudCam.getWindowSize().y)};
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
        const std::string Style = StyleName.empty() ? std::string("none") : StyleName;
        if (Pixel) {
            debug::setPanel("render style",
                            std::format("{} (F7): pixel x{}, {}x{} at {:.0f} px/m{}", Style, Pixel->Scale,
                                        Pixel->LowResSizePx.x, Pixel->LowResSizePx.y, Pixel->PixelsPerMeter,
                                        getStyle(Vis, StyleName).PixelArt->Letterbox ? ", letterbox" : ""));
        } else {
            debug::setPanel("render style", std::format("{} (F7): window resolution", Style));
        }
    }
}

void BattleRenderer::drawWorld(sf::RenderTarget& Target) {
    const sf::Font& Font = Assets.getFont(assets::MonoFontPath);
    if (!Pixel) {
        drawRenderList(Target, FrameCam, Frame, Layer::Background, Layer::Effects, Font);
        return;
    }
    if (LowRes.getSize() != Pixel->LowResSizePx) {
        if (!LowRes.resize(Pixel->LowResSizePx)) {
            log::error("cannot create a {}x{} picture for the pixel mode", Pixel->LowResSizePx.x,
                       Pixel->LowResSizePx.y);
            drawRenderList(Target, OverlayCam, Frame, Layer::Background, Layer::Effects, Font);
            return;
        }
        LowRes.setSmooth(false);
    }
    LowRes.clear(sf::Color::Black);
    drawRenderList(LowRes, FrameCam, Frame, Layer::Background, Layer::Effects, Font);
    LowRes.display();

    // Nearest-neighbour upscale by a whole number: every low-resolution pixel
    // becomes a Scale x Scale square of the window.
    sf::Sprite Picture(LowRes.getTexture());
    const auto Scale = static_cast<float>(Pixel->Scale);
    Picture.setScale({Scale, Scale});
    Picture.setPosition({static_cast<float>(Pixel->OffsetPx.x), static_cast<float>(Pixel->OffsetPx.y)});
    Target.setView(HudCam.getScreenView());
    Target.draw(Picture);
}

void BattleRenderer::drawHud(sf::RenderTarget& Target) const {
    drawRenderList(Target, HudCam, Frame, Layer::Hud, Layer::Hud, Assets.getFont(assets::MonoFontPath));
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
    for (auto&& [Look, Pictures] : std::views::zip(Looks, Sprites)) {
        // A skin named by the fighter's look wins over the style's.
        FighterLook Styled = Look;
        if (Styled.Skin.empty()) Styled.Skin = getStyleSkin(Vis, StyleName);
        Pictures = resolveFighterSprites(Vis, Styled, Load);
    }
}

void BattleRenderer::settleStyle() {
    if (StyleOverridden && Vis.Styles.contains(StyleName)) return;
    StyleOverridden = false;
    StyleName = Vis.Style;
}

float BattleRenderer::getPixelArtDensity(const PixelArtParams& Params) const {
    if (Params.PixelsPerMeter) return *Params.PixelsPerMeter;
    const auto Skin = Vis.Skins.find(getStyleSkin(Vis, StyleName));
    return Skin == Vis.Skins.end() ? Vis.PixelsPerMeter : Skin->second.PixelsPerMeter;
}

} // namespace fighter::render
