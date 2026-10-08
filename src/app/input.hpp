//===- app/input.hpp - Keyboard input for two players -----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares InputSystem, which turns keyboard events into
/// PlayerCommands for two players sharing one keyboard
/// (docs/DEVELOPMENT_PLAN.md, task 0.7).
///
/// The system keeps the "is the key held" state from press and release events
/// and builds PlayerCommands from it once per simulation step. Bindings use
/// scancodes, the physical key position, so WASD works with any keyboard
/// layout.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <SFML/Window/Keyboard.hpp>

#include "combat/commands.hpp"

namespace fighter::app {

enum class Action : uint8_t { Left, Right, Up, Down, Block, Light, Heavy, Kick, Special, Count };

struct Binding {
    sf::Keyboard::Scancode Key;
    int Player;   ///< 0 is the left player, 1 is the right one.
    Action Act;
};

class InputSystem {
public:
    static constexpr int PlayerCount = 2;

    InputSystem() : InputSystem(getDefaultBindings()) {}
    explicit InputSystem(std::vector<Binding> NewBindings);

    /// The default layout (task 2.0.2):
    ///   P1: A/D walk, W up, S down, F Light, G Heavy, R Kick, T Special,
    ///       Left Shift block.
    ///   P2: Left/Right walk, Up up, Down down, K Light, L Heavy, I Kick,
    ///       O Special, Right Shift block.
    static std::vector<Binding> getDefaultBindings();

    /// Returns true if the key is bound to some action.
    bool onKey(sf::Keyboard::Scancode Key, bool Pressed);

    /// Releases every key (the window lost focus; otherwise a key would stick).
    void reset();

    combat::PlayerCommands getCommands(int Player) const;

private:
    using ActionState = std::array<bool, static_cast<size_t>(Action::Count)>;

    std::vector<Binding> Bindings;
    std::array<ActionState, PlayerCount> Held{};
};

} // namespace fighter::app
