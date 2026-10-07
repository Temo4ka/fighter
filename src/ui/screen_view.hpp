//===- ui/screen_view.hpp - Drawing of the menus and screens ----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares drawScreen(), the placeholder look of the menus: text
/// and plain rectangles in the monospace font.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string>

#include <SFML/Graphics/RenderTarget.hpp>

#include "combat/result.hpp"
#include "render/resources.hpp"
#include "ui/screen_flow.hpp"

namespace fighter::ui {

/// What the screens show besides the flow itself.
struct ScreenContext {
    const combat::BattleResult* Result = nullptr;   ///< For the results screen.
    std::array<std::string, 2> Names;               ///< The fighters of the battle.
};

/// Draws the screen of \p Flow. In a battle nothing is drawn; on the pause
/// and the results a dimming layer and a panel go over the world drawn before.
void drawScreen(sf::RenderTarget& Target, render::Resources& Assets, const ScreenFlow& Flow,
                const ScreenContext& Context);

} // namespace fighter::ui
