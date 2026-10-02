//===- render/debug_overlay.hpp - Debug layer -------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares DebugOverlay, the debug layer of the debug build
/// (docs/DEVELOPMENT_PLAN.md, section 3.5). It draws the primitives of
/// debug::DrawList over the scene or instead of it, and the text panel.
///
/// Built only with FIGHTER_DEBUG=ON.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstddef>

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Keyboard.hpp>

#include "debug/draw_list.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

namespace fighter::render {

enum class ViewMode {
    DebugOnly,     ///< F1: primitives only, on a dark background with a 1 m grid.
    Both,          ///< F2: textures with primitives on top.
    TexturesOnly,  ///< F3: as in release.
};

/// Actions carried out by the application; the overlay does not own them.
enum class DebugAction { None, TogglePause, Step, Slower, Faster, Restart, Reload, ToggleShowcase };

class DebugOverlay {
public:
    struct KeyResult {
        bool Consumed = false;   ///< The key is a debug key and must not reach the game.
        DebugAction Action = DebugAction::None;
    };

    explicit DebugOverlay(Resources& Res);

    KeyResult handleKey(sf::Keyboard::Scancode Key);

    ViewMode getMode() const { return Mode; }
    void setMode(ViewMode NewMode) { Mode = NewMode; }
    bool shouldShowTextures() const { return Mode != ViewMode::DebugOnly; }
    bool shouldShowPrimitives() const { return Mode != ViewMode::TexturesOnly; }

    /// Background and grid for the DebugOnly mode.
    void drawBackdrop(sf::RenderTarget& Target, const Camera& Cam) const;
    void drawPrimitives(sf::RenderTarget& Target, const Camera& Cam, const debug::DrawList& List) const;
    void drawPanel(sf::RenderTarget& Target, const Camera& Cam, const debug::DrawList& List) const;

    bool isCategoryEnabled(debug::Cat Category) const { return Enabled.test(static_cast<size_t>(Category)); }

private:
    Resources& Assets;
    ViewMode Mode = ViewMode::DebugOnly;
    bool PanelVisible = true;
    std::bitset<debug::CatCount> Enabled;
};

} // namespace fighter::render
