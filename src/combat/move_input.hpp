//===- combat/move_input.hpp - Strike input: buttons, direction -*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MoveInput, what a player does to start a strike: the
/// attack buttons pressed together and the direction held, relative to the
/// opponent (decision 2026-10-08: a strike is chosen by button + direction).
/// A moveset (combat/moveset.hpp) maps inputs to moves.
///
/// An input is written in data as directions and buttons joined by '+':
///
///   "Light"               neutral, one button;
///   "Forward+Heavy"       towards the opponent;
///   "DownForward+Kick"    down and towards the opponent;
///   "Light+Heavy"         two buttons pressed together.
///
/// InputRules (data/input.json) say which direction stands in for another
/// when a moveset has no move for it, so "DownForward+Kick" falls back to
/// "Down+Kick" and then to "Kick" unless the data says otherwise.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "combat/commands.hpp"

namespace fighter::combat {

/// The attack buttons of PlayerCommands.
enum class AttackButton : uint8_t { Light, Heavy, Kick, Special, Count };

inline constexpr size_t AttackButtonCount = static_cast<size_t>(AttackButton::Count);

/// Every attack button, in declaration order.
inline constexpr std::array<AttackButton, AttackButtonCount> AttackButtons = {
    AttackButton::Light, AttackButton::Heavy, AttackButton::Kick, AttackButton::Special};

/// The direction held when a strike starts, relative to the opponent:
/// Forward is towards it.
enum class InputDirection : uint8_t {
    Neutral, Forward, Back, Up, Down, UpForward, UpBack, DownForward, DownBack, Count
};

inline constexpr size_t InputDirectionCount = static_cast<size_t>(InputDirection::Count);

/// Is the button \p Button held in \p Cmd?
constexpr bool isPressed(const PlayerCommands& Cmd, AttackButton Button) {
    switch (Button) {
        case AttackButton::Light: return Cmd.Light;
        case AttackButton::Heavy: return Cmd.Heavy;
        case AttackButton::Kick: return Cmd.Kick;
        case AttackButton::Special: return Cmd.Special;
        case AttackButton::Count: break;
    }
    return false;
}

/// The direction \p Cmd holds for a fighter facing right (\p FacingRight) or
/// left. A horizontal input within MoveDeadZone does not count; Down wins
/// over Up when both are held, as in getBlockZone().
constexpr InputDirection getInputDirection(const PlayerCommands& Cmd, bool FacingRight) {
    const float Forward = FacingRight ? Cmd.MoveX : -Cmd.MoveX;
    const int Horizontal = Forward > MoveDeadZone ? 1 : Forward < -MoveDeadZone ? -1 : 0;
    if (Cmd.Down) {
        return Horizontal > 0 ? InputDirection::DownForward
               : Horizontal < 0 ? InputDirection::DownBack
                                : InputDirection::Down;
    }
    if (Cmd.Up) {
        return Horizontal > 0 ? InputDirection::UpForward
               : Horizontal < 0 ? InputDirection::UpBack
                                : InputDirection::Up;
    }
    return Horizontal > 0 ? InputDirection::Forward
           : Horizontal < 0 ? InputDirection::Back
                            : InputDirection::Neutral;
}

/// Does the direction hold Down (Down, DownForward, DownBack)?
constexpr bool isDownward(InputDirection Direction) {
    return Direction == InputDirection::Down || Direction == InputDirection::DownForward ||
           Direction == InputDirection::DownBack;
}

/// A set of attack buttons pressed together.
class ButtonSet {
public:
    constexpr ButtonSet() = default;
    constexpr explicit ButtonSet(AttackButton Button) { add(Button); }

    constexpr void add(AttackButton Button) { Bits |= getBit(Button); }
    constexpr bool contains(AttackButton Button) const { return (Bits & getBit(Button)) != 0; }
    /// Is every button of \p Other in this set?
    constexpr bool containsAll(ButtonSet Other) const { return (Bits & Other.Bits) == Other.Bits; }
    constexpr bool intersects(ButtonSet Other) const { return (Bits & Other.Bits) != 0; }
    constexpr bool isEmpty() const { return Bits == 0; }
    constexpr size_t getSize() const {
        size_t Size = 0;
        for (const AttackButton Button : AttackButtons) Size += contains(Button) ? 1 : 0;
        return Size;
    }

    constexpr bool operator==(const ButtonSet&) const = default;
    /// The buttons of both sets.
    constexpr ButtonSet operator|(ButtonSet Other) const {
        ButtonSet Both;
        Both.Bits = static_cast<uint8_t>(Bits | Other.Bits);
        return Both;
    }

private:
    static constexpr uint8_t getBit(AttackButton Button) {
        return static_cast<uint8_t>(1u << static_cast<unsigned>(Button));
    }

    uint8_t Bits = 0;
};

/// The attack buttons held in \p Cmd.
constexpr ButtonSet getHeldButtons(const PlayerCommands& Cmd) {
    ButtonSet Held;
    for (const AttackButton Button : AttackButtons) {
        if (isPressed(Cmd, Button)) Held.add(Button);
    }
    return Held;
}

/// The attack buttons held in \p Cmd but not in \p Previous.
constexpr ButtonSet getNewlyPressed(const PlayerCommands& Cmd, const PlayerCommands& Previous) {
    ButtonSet Pressed;
    for (const AttackButton Button : AttackButtons) {
        if (isPressed(Cmd, Button) && !isPressed(Previous, Button)) Pressed.add(Button);
    }
    return Pressed;
}

/// What starts a strike: a direction and one or more buttons.
struct MoveInput {
    InputDirection Direction = InputDirection::Neutral;
    ButtonSet Buttons;

    constexpr bool operator==(const MoveInput&) const = default;
};

/// The attack buttons pressed lately: buttons pressed within
/// InputRules::ComboWindowSec of each other count as pressed together
/// ("Light+Heavy" pressed one tick apart). A button stays in the window,
/// held or released, until it is older than the window.
class PressWindow {
public:
    /// One step of \p Dt: the buttons already in the window age, those
    /// older than \p WindowSec drop out, \p Pressed (newly pressed now)
    /// come in at age 0.
    void update(ButtonSet Pressed, float WindowSec, float Dt);
    /// Empties the window (a move took its buttons).
    void clear();
    /// The buttons in the window.
    ButtonSet getButtons() const;
    /// Since the first button in the window was pressed, s; 0 if empty.
    float getAgeSec() const;

private:
    /// Per button: since it was pressed, s; negative: not in the window.
    std::array<float, AttackButtonCount> AgeSec{-1.0f, -1.0f, -1.0f, -1.0f};
};

/// The rules of data/input.json (docs/DATA_FORMATS.md).
struct InputRules {
    /// For each direction, the directions tried after it, in order, when a
    /// moveset has no move for it. Neutral is always tried last and need not
    /// be listed.
    std::array<std::vector<InputDirection>, InputDirectionCount> Fallbacks{};
    /// Buttons pressed within this time of each other count as pressed
    /// together ("Light+Heavy"), s.
    float ComboWindowSec = 0.05f;

    /// The directions to try for \p Direction: itself, its fallbacks, Neutral
    /// (each once).
    std::vector<InputDirection> getTryOrder(InputDirection Direction) const;

    /// Fallbacks to the nearest simpler direction (DownForward -> Down,
    /// UpBack -> Up -> Back ...) and a 0.05 s combo window, the values of
    /// data/input.json (a test keeps them equal).
    static InputRules getDefaults();
};

constexpr std::string_view getAttackButtonName(AttackButton Button) {
    constexpr std::array<std::string_view, AttackButtonCount> Names = {"Light", "Heavy", "Kick", "Special"};
    const auto Index = static_cast<size_t>(Button);
    return Index < Names.size() ? Names[Index] : "?";
}

constexpr std::string_view getInputDirectionName(InputDirection Direction) {
    constexpr std::array<std::string_view, InputDirectionCount> Names = {
        "Neutral", "Forward", "Back", "Up", "Down", "UpForward", "UpBack", "DownForward", "DownBack"};
    const auto Index = static_cast<size_t>(Direction);
    return Index < Names.size() ? Names[Index] : "?";
}

/// The button or direction with this name, or nullopt. Names are
/// case-sensitive.
std::optional<AttackButton> findAttackButton(std::string_view Name);
std::optional<InputDirection> findInputDirection(std::string_view Name);

/// Parses "DownForward+Kick": at most one direction (none: Neutral) and at
/// least one button, each named once. Throws std::runtime_error that names
/// the bad part.
MoveInput parseMoveInput(std::string_view Text);

/// "DownForward+Kick", "Light+Heavy", "Kick": the form parseMoveInput()
/// reads (Neutral is left out).
std::string formatMoveInput(const MoveInput& Input);

/// Parses data/input.json. Throws std::runtime_error that names the field.
InputRules parseInputRules(std::string_view JsonText);

/// Reads data/input.json; errors name the file.
InputRules loadInputRules(const std::filesystem::path& Path);

} // namespace fighter::combat
