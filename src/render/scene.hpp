//===- render/scene.hpp - A fight as a render list --------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares buildRenderList(), which turns a snapshot of a fight
/// into the generic primitives of a RenderList: background, arena, both
/// fighters part by part, effects and the HUD.
///
/// It is the only place that knows how a fight looks; it does no drawing and
/// needs no window, so the layer order, mirroring and fallbacks are tested
/// directly (tests/render/test_scene.cpp).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>

#include <SFML/Graphics/Texture.hpp>

#include "combat/snapshot.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "render/effects.hpp"
#include "render/render_list.hpp"
#include "render/sprites.hpp"
#include "render/visuals.hpp"

namespace fighter::render {

/// What a frame is built from besides the snapshot.
struct SceneInput {
    /// Pictures of the left and the right fighter; nullptr: capsules only.
    std::array<const FighterSprites*, 2> Sprites{};
    std::array<std::string, 2> Names;
    /// The fighter drawn behind the other (BattleEffects::getFarFighter()).
    size_t FarFighter = 1;
    const sf::Texture* Background = nullptr;   ///< nullptr: a plain color.
    Vec2 ViewCenterM{0.0f, 2.5f};              ///< The visible world, for the background and the floor.
    Vec2 ViewSizeM{10.0f, 6.0f};
    Vec2 ScreenSizePx{1280.0f, 720.0f};        ///< For the HUD layout.
    const BattleEffects* Effects = nullptr;
    double FrameTick = 0.0;                    ///< getFrameTick() of the frame.
};

/// The order in which a fighter's parts are drawn, from the back: the far
/// arm, the far leg, pelvis, torso, the near leg, head, the near arm
/// (docs/ART.md). Equipment goes right after its part.
std::span<const BodyPart, BodyPartCount> getPartDrawOrder();

RenderList buildRenderList(const combat::RenderSnapshot& Snapshot, const Visuals& Vis, const SceneInput& Scene);

} // namespace fighter::render
