//===- render/battle_renderer.hpp - Game view of a fight --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares BattleRenderer, which draws a fight the way the player
/// sees it: background, arena, fighters and the HUD.
///
/// In phase 0 fighters are colored placeholder rectangles. Agent C adds cutout
/// sprites for body parts once PartTransform data exists (phase 2).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <SFML/Graphics/RenderTarget.hpp>

#include "combat/snapshot.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

namespace fighter::render {

class BattleRenderer {
public:
    explicit BattleRenderer(Resources& Res);

    void drawWorld(sf::RenderTarget& Target, const Camera& Cam, const combat::RenderSnapshot& Snapshot);
    void drawHud(sf::RenderTarget& Target, const Camera& Cam, const combat::RenderSnapshot& Snapshot);

private:
    Resources& Assets;
};

} // namespace fighter::render
