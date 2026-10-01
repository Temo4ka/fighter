//===- app/app.hpp - Sandbox application ------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares App, the sandbox that runs a fight: window, input,
/// simulation loop and rendering.
///
/// The main menu, pause and results screens are added by agent F (phase 2).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <SFML/Graphics/RenderWindow.hpp>

#include "app/input.hpp"
#include "combat/battle.hpp"
#include "core/fixed_step_loop.hpp"
#include "render/battle_renderer.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"

#if FIGHTER_DEBUG
#include "render/debug_overlay.hpp"
#endif

namespace fighter::app {

struct Options {
    std::filesystem::path Root;                       ///< Project root (assets/, data/).
    std::optional<std::filesystem::path> Screenshot;  ///< Save a frame and exit.
    int Frames = 60;                                  ///< Frame on which the screenshot is taken.
    std::optional<std::string> Mode;                  ///< debug | both | textures (debug build only).
    bool Showcase = false;                            ///< Samples of every debug category.
};

class App {
public:
    explicit App(Options Settings);
    int run();

private:
    void handleEvents();
    void onKeyPressed(sf::Keyboard::Scancode Key);
    void stepSimulation(double Dt);
    void render(float Alpha);
    void restartBattle();
    void publishFrameStats(double FrameSec);
    void saveScreenshot();

#if FIGHTER_DEBUG
    void applyDebugAction(render::DebugAction Action);
#endif

    Options Opts;
    sf::RenderWindow Window;
    render::Resources Assets;
    render::Camera Cam;
    render::BattleRenderer Renderer;
#if FIGHTER_DEBUG
    render::DebugOverlay Overlay;
    bool ShowcaseVisible = false;
#endif

    InputSystem Input;
    FixedStepLoop Loop;
    std::unique_ptr<combat::Battle> CurrentBattle;
    combat::RenderSnapshot Previous;
    bool ResultReported = false;
    double FrameSecSmoothed = 0.0;
};

} // namespace fighter::app
