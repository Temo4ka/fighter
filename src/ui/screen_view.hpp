//===- ui/screen_view.hpp - Drawing of the menus and screens ----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ScreenView, which draws the menus and screens: panels,
/// bars and text in the monospace font, coloured by the palette of
/// data/ui.json. No art.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>

#include <SFML/Graphics/RenderTarget.hpp>

#include "combat/result.hpp"
#include "render/resources.hpp"
#include "ui/fighter_card.hpp"
#include "ui/screen_flow.hpp"

namespace fighter::ui {

/// What the screens show besides the flow itself.
struct ScreenContext {
    const combat::BattleResult* Result = nullptr;   ///< For the results screen.
    std::array<std::string, 2> Names;               ///< The fighters of the battle.
    std::array<float, 2> MaxHp{0.0f, 0.0f};         ///< For the HP bars of the results.
};

class ScreenView {
public:
    /// \p DataDir is the project's data/ directory (fighter sheets for the cards).
    ScreenView(render::Resources& Assets, std::filesystem::path DataDir);

    /// Advances the animation of the selection bar; called every frame.
    void update(double Dt, const ScreenFlow& Flow);

    /// Draws the screen of \p Flow. In a battle nothing is drawn; on the pause
    /// and the results a dimming layer goes over the world drawn before, the
    /// other screens draw their own backdrop.
    void draw(sf::RenderTarget& Target, const ScreenFlow& Flow, const ScreenContext& Context);

private:
    const FighterCard& getCard(const std::string& FileName);

    render::Resources& Assets;
    std::filesystem::path DataDir;
    std::map<std::string, FighterCard> Cards;
    Screen LastScreen = Screen::Battle;
    float HighlightPos = 0.0f;   ///< Eased position of the selection bar, in items.
};

} // namespace fighter::ui
