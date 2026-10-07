//===- ui/screen_flow.hpp - Screen flow state machine -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ScreenFlow, the state machine behind the menus: main
/// menu -> fighter selection -> battle -> pause -> results -> menu. It knows
/// nothing about windows or drawing: it takes abstract keys and battle
/// events and reports what the application has to do through a Signal.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "combat/events.hpp"
#include "core/signal.hpp"
#include "ui/ui_config.hpp"

namespace fighter::ui {

enum class Screen : uint8_t { MainMenu, FighterSelect, Battle, Pause, Results };

enum class MenuKey : uint8_t { Up, Down, Left, Right, Confirm, Back };

/// What the application has to do; emitted by ScreenFlow::onKey().
enum class FlowCommand : uint8_t {
    StartBattle,     ///< Create a battle of the picked fighters (getPickedFighter()).
    RestartBattle,   ///< Create a new battle of the same fighters.
    Quit,            ///< Close the application.
};

class ScreenFlow {
public:
    /// \p Fighters are the names selectable on the selection screen, see
    /// listFighters().
    explicit ScreenFlow(std::vector<std::string> FighterNames, UiConfig Settings = {},
                        Screen First = Screen::MainMenu);

    [[nodiscard]] Connection connectCommands(Signal<FlowCommand>::Handler Callback) {
        return Commands.connect(std::move(Callback));
    }

    /// A key of the menu. Back in a battle opens the pause.
    void onKey(MenuKey Key);

    /// The events of a battle step (the same Signal that feeds the renderer).
    /// BattleOver starts the countdown to the results screen.
    void onBattleEvent(const combat::BattleEvent& Event);

    /// Advances the countdown to the results; only a running battle counts.
    void update(double Dt);

    /// The new tuning after F5.
    void setConfig(const UiConfig& Settings) { Config = Settings; }

    /// A battle is running now, without a command: the sandbox starts this
    /// way, and so does a debug restart (Backspace, F5).
    void beginBattle();

    /// The battle for a StartBattle command could not be created (a broken
    /// data file): back to the selection.
    void onStartFailed();

    Screen getScreen() const { return Current; }

    /// The entries of the current menu (main menu, pause, results) and the
    /// selected one. Empty on the other screens.
    std::span<const std::string_view> getItems() const;
    size_t getCursor() const { return Cursor; }

    /// The selection screen: the list, the side being picked (0 left, 1 right)
    /// and the choice of each side (an index of getFighters()).
    std::span<const std::string> getFighters() const { return Fighters; }
    int getActiveSide() const { return Side; }
    size_t getPick(int SideIndex) const { return Picks[static_cast<size_t>(SideIndex)]; }
    /// The name picked for a side; empty when there are no fighters at all.
    const std::string& getPickedFighter(int SideIndex) const;

    bool isResultPending() const { return ResultPending; }

private:
    void enter(Screen Next);
    void enterFreshBattle();
    void confirm();
    void goBack();
    void moveCursor(int Step);

    UiConfig Config;
    std::vector<std::string> Fighters;
    Signal<FlowCommand> Commands;
    Screen Current;
    size_t Cursor = 0;
    int Side = 0;
    std::array<size_t, 2> Picks{0, 0};
    bool ResultPending = false;
    double ResultCountdownSec = 0.0;
};

} // namespace fighter::ui
