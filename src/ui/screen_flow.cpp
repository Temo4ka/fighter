#include "ui/screen_flow.hpp"

#include <algorithm>
#include <array>
#include <variant>

namespace fighter::ui {
namespace {

constexpr std::array<std::string_view, 2> MainMenuItems = {"Fight", "Quit"};
constexpr std::array<std::string_view, 3> PauseItems = {"Resume", "Restart", "Main menu"};
constexpr std::array<std::string_view, 3> ResultsItems = {"Rematch", "Fighter select", "Main menu"};

size_t wrapCursor(size_t Cursor, int Step, size_t Count);

} // namespace

ScreenFlow::ScreenFlow(std::vector<std::string> FighterNames, UiConfig Settings, Screen First)
    : Config(Settings), Fighters(std::move(FighterNames)), Current(First) {
    // Two different fighters by default, so that the first fight is not a mirror.
    if (Fighters.size() > 1) Picks[1] = 1;
}

void ScreenFlow::onKey(MenuKey Key) {
    switch (Key) {
        case MenuKey::Up: moveCursor(-1); break;
        case MenuKey::Down: moveCursor(1); break;
        case MenuKey::Left:
        case MenuKey::Right:
            // On the selection screen the horizontal keys move between the sides.
            if (Current == Screen::FighterSelect) {
                Side = Key == MenuKey::Left ? 0 : 1;
            } else if (Current == Screen::Results) {
                moveCursor(Key == MenuKey::Left ? -1 : 1);   // the buttons lie in a row
            }
            break;
        case MenuKey::Confirm: confirm(); break;
        case MenuKey::Back: goBack(); break;
    }
}

void ScreenFlow::onBattleEvent(const combat::BattleEvent& Event) {
    if (!std::holds_alternative<combat::BattleOver>(Event) || Current != Screen::Battle) return;
    ResultPending = true;
    ResultCountdownSec = Config.ResultsDelaySec;
}

void ScreenFlow::update(double Dt) {
    if (Current != Screen::Battle || !ResultPending) return;
    ResultCountdownSec -= Dt;
    if (ResultCountdownSec <= 0.0) enter(Screen::Results);
}

void ScreenFlow::beginBattle() {
    enterFreshBattle();
}

void ScreenFlow::onStartFailed() {
    enter(Screen::FighterSelect);
}

std::span<const std::string_view> ScreenFlow::getItems() const {
    switch (Current) {
        case Screen::MainMenu: return MainMenuItems;
        case Screen::Pause: return PauseItems;
        case Screen::Results: return ResultsItems;
        case Screen::FighterSelect:
        case Screen::Battle: break;
    }
    return {};
}

const std::string& ScreenFlow::getPickedFighter(int SideIndex) const {
    static const std::string None;
    return Fighters.empty() ? None : Fighters[getPick(SideIndex)];
}

void ScreenFlow::enter(Screen Next) {
    Current = Next;
    Cursor = 0;
    Side = 0;
}

void ScreenFlow::enterFreshBattle() {
    ResultPending = false;
    enter(Screen::Battle);
}

void ScreenFlow::confirm() {
    switch (Current) {
        case Screen::MainMenu:
            if (Cursor == 0) {
                enter(Screen::FighterSelect);
            } else {
                Commands.emit(FlowCommand::Quit);
            }
            break;
        case Screen::FighterSelect:
            if (Fighters.empty()) break;
            if (Side == 0) {
                Side = 1;
            } else {
                enterFreshBattle();
                Commands.emit(FlowCommand::StartBattle);
            }
            break;
        case Screen::Battle:
            break;
        case Screen::Pause:
            if (Cursor == 0) {
                enter(Screen::Battle);   // the result countdown survives a pause
            } else if (Cursor == 1) {
                enterFreshBattle();
                Commands.emit(FlowCommand::RestartBattle);
            } else {
                enter(Screen::MainMenu);
            }
            break;
        case Screen::Results:
            if (Cursor == 0) {
                enterFreshBattle();
                Commands.emit(FlowCommand::RestartBattle);
            } else if (Cursor == 1) {
                enter(Screen::FighterSelect);
            } else {
                enter(Screen::MainMenu);
            }
            break;
    }
}

void ScreenFlow::goBack() {
    switch (Current) {
        case Screen::MainMenu: break;
        case Screen::FighterSelect:
            if (Side == 1) {
                Side = 0;
            } else {
                enter(Screen::MainMenu);
            }
            break;
        case Screen::Battle: enter(Screen::Pause); break;
        case Screen::Pause: enter(Screen::Battle); break;
        case Screen::Results: enter(Screen::MainMenu); break;
    }
}

void ScreenFlow::moveCursor(int Step) {
    if (Current == Screen::FighterSelect) {
        Picks[static_cast<size_t>(Side)] = wrapCursor(Picks[static_cast<size_t>(Side)], Step, Fighters.size());
    } else {
        Cursor = wrapCursor(Cursor, Step, getItems().size());
    }
}

namespace {

size_t wrapCursor(size_t Cursor, int Step, size_t Count) {
    if (Count == 0) return 0;
    return static_cast<size_t>(static_cast<int>(Cursor + Count) + Step) % Count;
}

} // namespace

} // namespace fighter::ui
