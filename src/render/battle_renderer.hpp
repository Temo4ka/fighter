//===- render/battle_renderer.hpp - Game view of a fight --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares BattleRenderer, which draws a fight the way the player
/// sees it: background, arena, fighters, effects and the HUD.
///
/// Each frame goes through a render list: buildFrame() turns the snapshot
/// into primitives (scene.hpp), drawWorld() and drawHud() draw them. The
/// renderer owns what a fight looks like: data/visuals.json, the pictures of
/// both fighters and the effects started by battle events.
///
/// The style of visuals.json (or the one switched to by a debug key) picks
/// the skin and, in the pixel render mode, makes drawWorld() draw the world
/// into a low-resolution picture scaled up a whole number of times
/// (pixel_view.hpp). The HUD is always drawn at window resolution.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#include "combat/events.hpp"
#include "combat/snapshot.hpp"
#include "render/camera.hpp"
#include "render/effects.hpp"
#include "render/pixel_view.hpp"
#include "render/render_list.hpp"
#include "render/resources.hpp"
#include "render/sprites.hpp"
#include "render/visuals.hpp"

namespace fighter::render {

class BattleRenderer {
public:
    /// Reads data/visuals.json under the resources root; a broken or
    /// missing file is logged and the defaults are used. \p StepSec is the
    /// simulation step, the clock of the effects.
    BattleRenderer(Resources& Res, double StepSec);

    /// Re-reads data/visuals.json and the pictures (F5). On error keeps the
    /// current visuals, reports the error and returns false. A style picked
    /// with selectStyle() stays while the file still has it.
    bool reloadVisuals();

    /// Switches to the style \p Name of visuals.json "styles" and looks up
    /// the pictures again; false if there is no such style.
    bool selectStyle(std::string_view Name);
    /// Switches to the next style in the file (a debug key); returns its name.
    const std::string& cycleStyle();
    /// The style in use; empty when visuals.json has no styles.
    const std::string& getStyleName() const { return StyleName; }

    /// A new fight: who looks how. Forgets the effects of the previous one.
    void startBattle(std::array<FighterLook, 2> Looks);

    /// Starts the effects of an event; \p After is the snapshot right after
    /// the step that produced it.
    void onBattleEvent(const combat::BattleEvent& Event, const combat::RenderSnapshot& After);

    /// Builds the frame of \p Snapshot, interpolated \p Alpha of the way from
    /// the previous step (combat::interpolate). Call once per frame before
    /// drawWorld() and drawHud().
    void buildFrame(const Camera& Cam, const combat::RenderSnapshot& Snapshot, float Alpha);
    /// Background, arena, fighters and effects; in the pixel mode through
    /// the low-resolution picture, which needs a graphics context.
    void drawWorld(sf::RenderTarget& Target);
    void drawHud(sf::RenderTarget& Target) const;

    const Visuals& getVisuals() const { return Vis; }
    const RenderList& getFrame() const { return Frame; }
    /// The layout of the pixel mode of the last frame; nullopt: smooth style.
    const std::optional<PixelLayout>& getPixelLayout() const { return Pixel; }
    /// A window-resolution camera that matches the drawn world of the last
    /// frame (shake and pixel grid included); debug primitives use it.
    const Camera& getOverlayCamera() const { return OverlayCam; }

private:
    /// Reads data/visuals.json into Vis; on error keeps Vis and returns false.
    bool loadVisualsFile();
    void resolveSprites();
    /// Keeps StyleName if the file has it, otherwise takes the file's style.
    void settleStyle();
    /// The density of the pixel mode: its own, or that of the style's skin.
    float getPixelArtDensity(const PixelArtParams& Params) const;

    Resources& Assets;
    Visuals Vis;
    std::array<FighterLook, 2> Looks;
    std::array<FighterSprites, 2> Sprites;
    BattleEffects Effects;
    RenderList Frame;
    std::string StyleName;
    /// true once a key picked the style: F5 then keeps it.
    bool StyleOverridden = false;
    Camera FrameCam;     ///< Draws the world: shaken, low-resolution in the pixel mode.
    Camera HudCam;       ///< The window, for the HUD.
    Camera OverlayCam;   ///< See getOverlayCamera().
    std::optional<PixelLayout> Pixel;
    sf::RenderTexture LowRes;   ///< Created on the first pixel-mode draw.
};

} // namespace fighter::render
