#include "app/input.hpp"

#include <cstddef>

namespace fighter::app {

InputSystem::InputSystem(std::vector<Binding> NewBindings) : Bindings(std::move(NewBindings)) {}

std::vector<Binding> InputSystem::getDefaultBindings() {
    using Scan = sf::Keyboard::Scan;
    return {
        {Scan::A, 0, Action::Left},       {Scan::D, 0, Action::Right},
        {Scan::W, 0, Action::Up},         {Scan::S, 0, Action::Down},
        {Scan::F, 0, Action::Jab},        {Scan::G, 0, Action::HeavyPunch},
        {Scan::R, 0, Action::BodyKick},   {Scan::T, 0, Action::LowKick},
        {Scan::LShift, 0, Action::Block},

        {Scan::Left, 1, Action::Left},    {Scan::Right, 1, Action::Right},
        {Scan::Up, 1, Action::Up},        {Scan::Down, 1, Action::Down},
        {Scan::K, 1, Action::Jab},        {Scan::L, 1, Action::HeavyPunch},
        {Scan::I, 1, Action::BodyKick},   {Scan::O, 1, Action::LowKick},
        {Scan::RShift, 1, Action::Block},
    };
}

bool InputSystem::onKey(sf::Keyboard::Scancode Key, bool Pressed) {
    bool Bound = false;
    for (const Binding& Entry : Bindings) {
        if (Entry.Key != Key || Entry.Player < 0 || Entry.Player >= PlayerCount) continue;
        Held[static_cast<size_t>(Entry.Player)][static_cast<size_t>(Entry.Act)] = Pressed;
        Bound = true;
    }
    return Bound;
}

void InputSystem::reset() {
    Held = {};
}

combat::PlayerCommands InputSystem::getCommands(int Player) const {
    if (Player < 0 || Player >= PlayerCount) return {};
    const ActionState& State = Held[static_cast<size_t>(Player)];
    auto IsHeld = [&](Action Act) { return State[static_cast<size_t>(Act)]; };

    combat::PlayerCommands Cmd;
    // Both directions at once means standing still.
    Cmd.MoveX = (IsHeld(Action::Right) ? 1.0f : 0.0f) - (IsHeld(Action::Left) ? 1.0f : 0.0f);
    Cmd.Up = IsHeld(Action::Up);
    Cmd.Down = IsHeld(Action::Down);
    Cmd.Block = IsHeld(Action::Block);
    Cmd.Jab = IsHeld(Action::Jab);
    Cmd.HeavyPunch = IsHeld(Action::HeavyPunch);
    Cmd.BodyKick = IsHeld(Action::BodyKick);
    Cmd.LowKick = IsHeld(Action::LowKick);
    return Cmd;
}

} // namespace fighter::app
