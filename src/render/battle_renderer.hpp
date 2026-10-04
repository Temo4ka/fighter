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
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include <SFML/Graphics/RenderTarget.hpp>

#include "combat/events.hpp"
#include "combat/snapshot.hpp"
#include "render/camera.hpp"
#include "render/effects.hpp"
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
    /// current visuals, reports the error and returns false.
    bool reloadVisuals();

    /// A new fight: who looks how. Forgets the effects of the previous one.
    void startBattle(std::array<FighterLook, 2> Looks);

    /// Starts the effects of an event; \p After is the snapshot right after
    /// the step that produced it.
    void onBattleEvent(const combat::BattleEvent& Event, const combat::RenderSnapshot& After);

    /// Builds the frame of \p Snapshot, interpolated \p Alpha of the way from
    /// the previous step (combat::interpolate). Call once per frame before
    /// drawWorld() and drawHud().
    void buildFrame(const Camera& Cam, const combat::RenderSnapshot& Snapshot, float Alpha);
    /// Background, arena, fighters and effects.
    void drawWorld(sf::RenderTarget& Target) const;
    void drawHud(sf::RenderTarget& Target) const;

    const Visuals& getVisuals() const { return Vis; }
    const RenderList& getFrame() const { return Frame; }

private:
    /// Reads data/visuals.json into Vis; on error keeps Vis and returns false.
    bool loadVisualsFile();
    void resolveSprites();

    Resources& Assets;
    Visuals Vis;
    std::array<FighterLook, 2> Looks;
    std::array<FighterSprites, 2> Sprites;
    BattleEffects Effects;
    RenderList Frame;
    Camera FrameCam;   ///< The camera of the frame, shaken by the effects.
};

} // namespace fighter::render
