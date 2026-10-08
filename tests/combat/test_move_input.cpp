#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <vector>

#include "combat/move_input.hpp"

using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

} // namespace

TEST_CASE("MoveInput: the direction is relative to the opponent", "[combat][input]") {
    CHECK(getInputDirection({}, true) == InputDirection::Neutral);
    CHECK(getInputDirection({.MoveX = 1.0f}, true) == InputDirection::Forward);
    CHECK(getInputDirection({.MoveX = 1.0f}, false) == InputDirection::Back);
    CHECK(getInputDirection({.MoveX = -1.0f, .Down = true}, false) == InputDirection::DownForward);
    CHECK(getInputDirection({.MoveX = -1.0f, .Up = true}, true) == InputDirection::UpBack);
    CHECK(getInputDirection({.Up = true, .Down = true}, true) == InputDirection::Down);
    CHECK(getInputDirection({.MoveX = 0.05f}, true) == InputDirection::Neutral);   // within the dead zone
}

TEST_CASE("MoveInput: held and newly pressed buttons", "[combat][input]") {
    const PlayerCommands Before{.Light = true};
    const PlayerCommands Now{.Light = true, .Kick = true};
    CHECK(getHeldButtons(Now).getSize() == 2);
    const ButtonSet Pressed = getNewlyPressed(Now, Before);
    CHECK(Pressed == ButtonSet(AttackButton::Kick));
    CHECK(getHeldButtons(Now).containsAll(Pressed));
    CHECK_FALSE(Pressed.intersects(ButtonSet(AttackButton::Light)));
}

TEST_CASE("MoveInput: parse and format", "[combat][input]") {
    const MoveInput Input = parseMoveInput("DownForward+Kick");
    CHECK(Input.Direction == InputDirection::DownForward);
    CHECK(Input.Buttons == ButtonSet(AttackButton::Kick));
    CHECK(formatMoveInput(Input) == "DownForward+Kick");

    const MoveInput Combo = parseMoveInput("Heavy + Light");
    CHECK(Combo.Direction == InputDirection::Neutral);
    CHECK(Combo.Buttons.getSize() == 2);
    CHECK(formatMoveInput(Combo) == "Light+Heavy");
    CHECK(parseMoveInput("Neutral+Special") == parseMoveInput("Special"));

    CHECK_THROWS_WITH(parseMoveInput("Forward"), ContainsSubstring("no button"));
    CHECK_THROWS_WITH(parseMoveInput("Up+Down+Kick"), ContainsSubstring("more than one direction"));
    CHECK_THROWS_WITH(parseMoveInput("Kick+Kick"), ContainsSubstring("named twice"));
    CHECK_THROWS_WITH(parseMoveInput("Jab"), ContainsSubstring("'Jab' is neither a button"));
    CHECK_THROWS_WITH(parseMoveInput("Light+"), ContainsSubstring("an empty part"));
}

TEST_CASE("InputRules: fallbacks end with Neutral", "[combat][input]") {
    const InputRules Rules = parseInputRules(R"({"combo_window_sec": 0.1,
        "fallbacks": {"DownForward": ["Down", "Forward"]}})");
    CHECK(Rules.ComboWindowSec == 0.1f);
    CHECK(Rules.getTryOrder(InputDirection::DownForward) ==
          std::vector{InputDirection::DownForward, InputDirection::Down, InputDirection::Forward,
                      InputDirection::Neutral});
    CHECK(Rules.getTryOrder(InputDirection::Neutral) == std::vector{InputDirection::Neutral});
    CHECK(Rules.getTryOrder(InputDirection::Back) == std::vector{InputDirection::Back, InputDirection::Neutral});

    CHECK_THROWS_WITH(parseInputRules(R"({"fallbacks": {"Sideways": []}})"), ContainsSubstring("unknown direction"));
    CHECK_THROWS_WITH(parseInputRules(R"({"fallbacks": {"Down": ["Down"]}})"), ContainsSubstring("repeated"));
    CHECK_THROWS_WITH(parseInputRules(R"({"combo": 1})"), ContainsSubstring("unknown field 'combo'"));
}

TEST_CASE("InputRules: data/input.json equals the defaults", "[combat][input][data]") {
    const InputRules Loaded = loadInputRules(DataDir / "input.json");
    const InputRules Defaults = InputRules::getDefaults();
    CHECK(Loaded.ComboWindowSec == Defaults.ComboWindowSec);
    CHECK(Loaded.Fallbacks == Defaults.Fallbacks);
}

TEST_CASE("PressWindow: presses within the window count together", "[combat][input]") {
    constexpr float Window = 0.05f;
    constexpr float Tick = 1.0f / 60.0f;
    PressWindow Presses;
    CHECK(Presses.getButtons().isEmpty());
    CHECK(Presses.getAgeSec() == 0.0f);

    Presses.update(ButtonSet(AttackButton::Light), Window, Tick);
    CHECK(Presses.getButtons() == ButtonSet(AttackButton::Light));
    Presses.update({}, Window, Tick);
    Presses.update(ButtonSet(AttackButton::Heavy), Window, Tick);
    ButtonSet Both(AttackButton::Light);
    Both.add(AttackButton::Heavy);
    CHECK(Presses.getButtons() == Both);
    CHECK(Presses.getAgeSec() > 1.5f * Tick);

    // Light is older than the window two ticks later; Heavy is not yet.
    Presses.update({}, Window, Tick);
    Presses.update({}, Window, Tick);
    CHECK(Presses.getButtons() == ButtonSet(AttackButton::Heavy));
    Presses.clear();
    CHECK(Presses.getButtons().isEmpty());

    // A window of 0 keeps only the presses of this step.
    Presses.update(ButtonSet(AttackButton::Kick), 0.0f, Tick);
    CHECK(Presses.getButtons() == ButtonSet(AttackButton::Kick));
    Presses.update({}, 0.0f, Tick);
    CHECK(Presses.getButtons().isEmpty());

    CHECK((ButtonSet(AttackButton::Light) | ButtonSet(AttackButton::Heavy)) == Both);
}
