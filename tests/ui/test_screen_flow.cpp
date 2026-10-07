#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ui/screen_flow.hpp"

using namespace fighter;
using namespace fighter::ui;

namespace {

struct Harness {
    explicit Harness(std::vector<std::string> Names = {"knight", "rogue", "thug"}, UiConfig Config = {})
        : Flow(std::move(Names), Config) {
        Link = Flow.connectCommands([this](FlowCommand Command) { Commands.push_back(Command); });
    }

    void press(std::initializer_list<MenuKey> Keys) {
        for (const MenuKey Key : Keys) Flow.onKey(Key);
    }

    /// Main menu -> selection -> battle.
    void startBattle() { press({MenuKey::Confirm, MenuKey::Confirm, MenuKey::Confirm}); }

    ScreenFlow Flow;
    Connection Link;
    std::vector<FlowCommand> Commands;
};

combat::BattleEvent makeBattleOver() {
    return combat::BattleOver{combat::Winner::Left, combat::BattleEnd::Knockout};
}

} // namespace

TEST_CASE("ScreenFlow: starts in the main menu with Fight selected", "[ui][flow]") {
    Harness Test;
    CHECK(Test.Flow.getScreen() == Screen::MainMenu);
    CHECK(Test.Flow.getCursor() == 0);
    CHECK(Test.Flow.getItems().size() == 2);
}

TEST_CASE("ScreenFlow: the cursor wraps around", "[ui][flow]") {
    Harness Test;
    Test.press({MenuKey::Up});
    CHECK(Test.Flow.getCursor() == 1);
    Test.press({MenuKey::Down});
    CHECK(Test.Flow.getCursor() == 0);
}

TEST_CASE("ScreenFlow: Quit emits the command", "[ui][flow]") {
    Harness Test;
    Test.press({MenuKey::Down, MenuKey::Confirm});
    REQUIRE(Test.Commands.size() == 1);
    CHECK(Test.Commands[0] == FlowCommand::Quit);
}

TEST_CASE("ScreenFlow: selection picks the left then the right fighter", "[ui][flow]") {
    Harness Test;
    Test.press({MenuKey::Confirm});
    REQUIRE(Test.Flow.getScreen() == Screen::FighterSelect);
    CHECK(Test.Flow.getActiveSide() == 0);

    Test.press({MenuKey::Down, MenuKey::Confirm});   // P1: rogue
    CHECK(Test.Flow.getActiveSide() == 1);
    Test.press({MenuKey::Down, MenuKey::Confirm});   // P2: thug (rogue is the default of P2, one down)
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    REQUIRE(Test.Commands.size() == 1);
    CHECK(Test.Commands[0] == FlowCommand::StartBattle);
    CHECK(Test.Flow.getPickedFighter(0) == "rogue");
    CHECK(Test.Flow.getPickedFighter(1) == "thug");
}

TEST_CASE("ScreenFlow: selection Back steps from the right side to the left, then to the menu", "[ui][flow]") {
    Harness Test;
    Test.press({MenuKey::Confirm, MenuKey::Confirm});
    REQUIRE(Test.Flow.getActiveSide() == 1);
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.getScreen() == Screen::FighterSelect);
    CHECK(Test.Flow.getActiveSide() == 0);
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.getScreen() == Screen::MainMenu);
}

TEST_CASE("ScreenFlow: horizontal keys switch the side on the selection screen", "[ui][flow]") {
    Harness Test;
    Test.press({MenuKey::Confirm, MenuKey::Right});
    CHECK(Test.Flow.getActiveSide() == 1);
    Test.press({MenuKey::Left});
    CHECK(Test.Flow.getActiveSide() == 0);
}

TEST_CASE("ScreenFlow: no fighters means no battle", "[ui][flow]") {
    Harness Test({});
    Test.press({MenuKey::Confirm, MenuKey::Confirm, MenuKey::Confirm});
    CHECK(Test.Flow.getScreen() == Screen::FighterSelect);
    CHECK(Test.Commands.empty());
    CHECK(Test.Flow.getPickedFighter(0).empty());
}

TEST_CASE("ScreenFlow: a failed start returns to the selection", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    REQUIRE(Test.Flow.getScreen() == Screen::Battle);
    Test.Flow.onStartFailed();
    CHECK(Test.Flow.getScreen() == Screen::FighterSelect);
}

TEST_CASE("ScreenFlow: pause opens on Back and resumes", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.getScreen() == Screen::Pause);
    CHECK(Test.Flow.getItems().size() == 3);
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    Test.press({MenuKey::Back, MenuKey::Confirm});   // Resume
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    CHECK(Test.Commands.size() == 1);   // only the StartBattle
}

TEST_CASE("ScreenFlow: pause Restart emits the command and returns to the battle", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    Test.press({MenuKey::Back, MenuKey::Down, MenuKey::Confirm});
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    REQUIRE(Test.Commands.size() == 2);
    CHECK(Test.Commands[1] == FlowCommand::RestartBattle);
}

TEST_CASE("ScreenFlow: pause Main menu leaves the battle", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    Test.press({MenuKey::Back, MenuKey::Up, MenuKey::Confirm});
    CHECK(Test.Flow.getScreen() == Screen::MainMenu);
}

TEST_CASE("ScreenFlow: results come after the delay once the battle is over", "[ui][flow]") {
    Harness Test({"knight", "rogue"}, UiConfig{.ResultsDelaySec = 1.0});
    Test.startBattle();

    Test.Flow.update(5.0);   // no result yet: nothing happens
    CHECK(Test.Flow.getScreen() == Screen::Battle);

    Test.Flow.onBattleEvent(makeBattleOver());
    CHECK(Test.Flow.isResultPending());
    Test.Flow.update(0.6);
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    Test.Flow.update(0.6);
    CHECK(Test.Flow.getScreen() == Screen::Results);
    CHECK(Test.Flow.getItems().size() == 3);
}

TEST_CASE("ScreenFlow: other events do not end the battle", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    Test.Flow.onBattleEvent(combat::Exhausted{0});
    CHECK_FALSE(Test.Flow.isResultPending());
}

TEST_CASE("ScreenFlow: the countdown stops during a pause and survives it", "[ui][flow]") {
    Harness Test({"knight", "rogue"}, UiConfig{.ResultsDelaySec = 1.0});
    Test.startBattle();
    Test.Flow.onBattleEvent(makeBattleOver());
    Test.press({MenuKey::Back});
    Test.Flow.update(10.0);
    CHECK(Test.Flow.getScreen() == Screen::Pause);
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.isResultPending());
    Test.Flow.update(1.5);
    CHECK(Test.Flow.getScreen() == Screen::Results);
}

TEST_CASE("ScreenFlow: a restart cancels a pending result", "[ui][flow]") {
    Harness Test;
    Test.startBattle();
    Test.Flow.onBattleEvent(makeBattleOver());
    Test.Flow.beginBattle();   // a debug restart
    CHECK_FALSE(Test.Flow.isResultPending());
}

TEST_CASE("ScreenFlow: results Rematch, Fighter select and Main menu", "[ui][flow]") {
    Harness Test({"knight", "rogue"}, UiConfig{.ResultsDelaySec = 0.0});
    Test.startBattle();
    Test.Flow.onBattleEvent(makeBattleOver());
    Test.Flow.update(0.01);
    REQUIRE(Test.Flow.getScreen() == Screen::Results);

    Test.press({MenuKey::Confirm});   // Rematch
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    CHECK(Test.Commands.back() == FlowCommand::RestartBattle);

    Test.Flow.onBattleEvent(makeBattleOver());
    Test.Flow.update(0.01);
    Test.press({MenuKey::Down, MenuKey::Confirm});   // Fighter select
    CHECK(Test.Flow.getScreen() == Screen::FighterSelect);

    Test.press({MenuKey::Confirm, MenuKey::Confirm});   // and fight again
    Test.Flow.onBattleEvent(makeBattleOver());
    Test.Flow.update(0.01);
    Test.press({MenuKey::Back});
    CHECK(Test.Flow.getScreen() == Screen::MainMenu);
}

TEST_CASE("ScreenFlow: the whole loop returns to the menu", "[ui][flow]") {
    Harness Test({"knight", "rogue"}, UiConfig{.ResultsDelaySec = 0.0});
    Test.startBattle();
    Test.Flow.onBattleEvent(makeBattleOver());
    Test.Flow.update(0.01);
    Test.press({MenuKey::Up, MenuKey::Confirm});   // Main menu
    CHECK(Test.Flow.getScreen() == Screen::MainMenu);
    Test.startBattle();
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    CHECK(Test.Commands.size() == 2);
}

TEST_CASE("ScreenFlow: BattleEvents Signal feeds the flow", "[ui][flow]") {
    Signal<const combat::BattleEvent&> Events;
    Harness Test({"a", "b"}, UiConfig{.ResultsDelaySec = 0.0});
    const Connection Link = Events.connect([&](const combat::BattleEvent& Event) { Test.Flow.onBattleEvent(Event); });
    Test.startBattle();
    Events.emit(makeBattleOver());
    Test.Flow.update(0.01);
    CHECK(Test.Flow.getScreen() == Screen::Results);
}

TEST_CASE("ScreenFlow: battle can begin without a command", "[ui][flow]") {
    Harness Test;
    Test.Flow.beginBattle();
    CHECK(Test.Flow.getScreen() == Screen::Battle);
    CHECK(Test.Commands.empty());
}
