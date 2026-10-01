#pragma once

#include <bitset>

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Keyboard.hpp>

#include "debug/draw_list.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

// Отладочный слой debug-сборки (docs/DEVELOPMENT_PLAN.md §3.5): рисует примитивы
// debug::DrawList поверх сцены или вместо неё и текстовую панель.
// Собирается только при FIGHTER_DEBUG=ON.
namespace fighter::render {

enum class ViewMode {
    DebugOnly,     // F1: только примитивы на тёмном фоне с сеткой 1 м
    Both,          // F2: текстуры + примитивы поверх
    TexturesOnly,  // F3: как в release
};

// Действия, которые выполняет приложение (overlay ими не владеет).
enum class DebugAction { None, TogglePause, Step, Slower, Faster, Restart, Reload, ToggleShowcase };

class DebugOverlay {
public:
    struct KeyResult {
        bool consumed = false;   // клавиша отладочная — в игру её не передаём
        DebugAction action = DebugAction::None;
    };

    explicit DebugOverlay(Resources& resources);

    KeyResult handleKey(sf::Keyboard::Scancode key);

    ViewMode mode() const { return mode_; }
    void setMode(ViewMode mode) { mode_ = mode; }
    bool showTextures() const { return mode_ != ViewMode::DebugOnly; }
    bool showPrimitives() const { return mode_ != ViewMode::TexturesOnly; }

    // Фон и сетка для режима DebugOnly.
    void drawBackdrop(sf::RenderTarget& target, const Camera& camera) const;
    void drawPrimitives(sf::RenderTarget& target, const Camera& camera, const debug::DrawList& list) const;
    void drawPanel(sf::RenderTarget& target, const Camera& camera, const debug::DrawList& list) const;

    bool categoryEnabled(debug::Cat cat) const { return enabled_.test(static_cast<std::size_t>(cat)); }

private:
    Resources& resources_;
    ViewMode mode_ = ViewMode::DebugOnly;
    bool panelVisible_ = true;
    std::bitset<debug::kCatCount> enabled_;
};

} // namespace fighter::render
