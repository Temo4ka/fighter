#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <SFML/Window/Keyboard.hpp>

#include "combat/commands.hpp"

// Ввод двух игроков с одной клавиатуры (docs/DEVELOPMENT_PLAN.md, задача 0.7).
//
// Хранит состояние «нажата ли клавиша» по событиям нажатия и отпускания, а раз в шаг
// симуляции собирает из него PlayerCommands. Используются scancode — физическое
// положение клавиши: WASD работает и в русской раскладке.
namespace fighter::app {

enum class Action : std::uint8_t { Left, Right, Jump, Crouch, Punch, Kick, Block, Count };

struct Binding {
    sf::Keyboard::Scancode key;
    int player;   // 0 — левый, 1 — правый
    Action action;
};

class InputSystem {
public:
    static constexpr int kPlayers = 2;

    InputSystem() : InputSystem(defaultBindings()) {}
    explicit InputSystem(std::vector<Binding> bindings);

    // Раскладка по умолчанию:
    //   P1: A/D — ходьба, W — прыжок, S — присед, F — рука, G — нога, H — блок
    //   P2: ←/→ — ходьба, ↑ — прыжок, ↓ — присед, K — рука, L — нога, ; — блок
    static std::vector<Binding> defaultBindings();

    // Возвращает true, если клавиша назначена какому-то действию.
    bool onKey(sf::Keyboard::Scancode key, bool pressed);

    // Отпустить все клавиши (окно потеряло фокус — иначе клавиша «залипнет»).
    void reset();

    combat::PlayerCommands commands(int player) const;

private:
    using ActionState = std::array<bool, static_cast<std::size_t>(Action::Count)>;

    std::vector<Binding> bindings_;
    std::array<ActionState, kPlayers> held_{};
};

} // namespace fighter::app
