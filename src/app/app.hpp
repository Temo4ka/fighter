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
/// The main menu, fighter selection, pause and results screens are in src/ui;
/// the App only routes keys and runs the battle that the flow asks for.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics/RenderWindow.hpp>

#include "app/demo_script.hpp"
#include "app/input.hpp"
#include "combat/battle.hpp"
#include "combat/move_measure.hpp"
#include "core/fixed_step_loop.hpp"
#include "core/signal.hpp"
#include "render/battle_renderer.hpp"
#include "render/camera.hpp"
#include "render/resources.hpp"
#include "ui/screen_flow.hpp"
#include "ui/screen_view.hpp"

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
    std::optional<std::string> Style;                 ///< A style of data/visuals.json instead of its "style".
    std::optional<DemoScript> Demo;                   ///< Scripted input instead of the keyboard.
    /// Fighter sheets, data/fighters/<name>.json, for each side; nullopt: the
    /// built-in sandbox fighter.
    std::optional<std::string> LeftFighter;
    std::optional<std::string> RightFighter;
    /// Start in the main menu instead of straight in a battle.
    bool Menu = false;
    /// Menu keys pressed one per frame from the start (--keys, for screenshots
    /// of the menu screens): up, down, left, right, enter, esc.
    std::vector<sf::Keyboard::Scancode> Keys;
    /// Round time instead of the default (--round, e.g. to reach the results
    /// screen quickly for a screenshot).
    std::optional<double> RoundSec;
    /// The move stand (--stand <move> [--weapon <item>]): one fighter repeats
    /// the move in front of a dummy (combat/move_measure.hpp). F5 reloads the
    /// move, its clip and the item.
    std::optional<std::string> StandMove;
    std::string StandWeapon;
};

/// Parses "up,down,left,right,enter,esc" (unknown names are skipped).
std::vector<sf::Keyboard::Scancode> parseMenuKeys(std::string_view List);

class App {
public:
    explicit App(Options Settings);
    int run();

private:
    void handleEvents();
    void onKeyPressed(sf::Keyboard::Scancode Key);
    void stepSimulation(double Dt);
    void render(float Alpha);
    /// Creates a new battle from the data files. If they are broken, keeps
    /// the current battle, reports the error and returns false.
    bool restartBattle();
    void publishFrameStats(double FrameSec);
    /// Records the stand run after a step, draws it and starts the next run
    /// when this one is over.
    void stepStand();
    void saveScreenshot();
    void onFlowCommand(ui::FlowCommand Command);
    /// Starts a battle of the fighters picked in the menu; false on a data error.
    bool startMenuBattle();
    bool hasBattleOnScreen() const;
    /// Keeps the arena behind the menus fresh: a battle of the picked fighters
    /// that has not started, so the select screen shows them standing.
    void refreshMenuStage(ui::Screen Before);
    void loadUiConfig();

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

    /// Skins of the left and the right fighter, keys of data/visuals.json
    /// "skins"; empty: its default_skin.
    std::array<std::string, 2> Skins;
    /// The events of each step go out through this signal (docs/ARCHITECTURE.md,
    /// "События"); the second argument is the snapshot after the step.
    Signal<const combat::BattleEvent&, const combat::RenderSnapshot&> BattleEvents;
    Connection RendererEvents;

    InputSystem Input;
    ui::ScreenFlow Flow;
    ui::ScreenView Screens;
    Connection FlowCommands;
    Connection FlowEvents;
    FixedStepLoop Loop;
    std::unique_ptr<combat::Battle> CurrentBattle;
    combat::RenderSnapshot Previous;
    bool ResultReported = false;
    /// The move stand (Opts.StandMove): what is being repeated, the current
    /// run, and the last run that finished, whose paths stay on the screen
    /// until the next run starts to move.
    std::optional<combat::StandSetup> Stand;
    std::unique_ptr<combat::MoveRun> StandRun;
    combat::MoveMeasure StandShown;
    bool StandLogged = false;
    /// Exponential moving average of the frame time, shown as FPS in the
    /// debug panel. Averaging the time rather than 1/time keeps rare fast
    /// frames from inflating the FPS.
    double FrameSecSmoothed = 0.0;
    /// The interpolation factor of the last simulated frame, kept while a pause freezes the battle.
    float LastAlpha = 1.0f;
};

} // namespace fighter::app
