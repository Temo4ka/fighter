#include "app/input.hpp"

namespace fighter::app {

InputSystem::InputSystem(std::vector<Binding> NewBindings) : Bindings(std::move(NewBindings)) {}

std::vector<Binding> InputSystem::getDefaultBindings() {
    using Scan = sf::Keyboard::Scan;
    return {
        {Scan::A, 0, Action::Left},     {Scan::D, 0, Action::Right},
        {Scan::W, 0, Action::Jump},     {Scan::S, 0, Action::Crouch},
        {Scan::F, 0, Action::Punch},    {Scan::G, 0, Action::Kick},
        {Scan::H, 0, Action::Block},

        {Scan::Left, 1, Action::Left},  {Scan::Right, 1, Action::Right},
        {Scan::Up, 1, Action::Jump},    {Scan::Down, 1, Action::Crouch},
        {Scan::K, 1, Action::Punch},    {Scan::L, 1, Action::Kick},
        {Scan::Semicolon, 1, Action::Block},
    };
}

bool InputSystem::onKey(sf::Keyboard::Scancode Key, bool Pressed) {
    bool Bound = false;
    for (const Binding& B : Bindings) {
        if (B.Key != Key || B.Player < 0 || B.Player >= PlayerCount) continue;
        Held[static_cast<std::size_t>(B.Player)][static_cast<std::size_t>(B.Act)] = Pressed;
        Bound = true;
    }
    return Bound;
}

void InputSystem::reset() {
    Held = {};
}

combat::PlayerCommands InputSystem::getCommands(int Player) const {
    if (Player < 0 || Player >= PlayerCount) return {};
    const ActionState& State = Held[static_cast<std::size_t>(Player)];
    auto IsHeld = [&](Action A) { return State[static_cast<std::size_t>(A)]; };

    combat::PlayerCommands Cmd;
    // Both directions at once means standing still.
    Cmd.MoveX = (IsHeld(Action::Right) ? 1.0f : 0.0f) - (IsHeld(Action::Left) ? 1.0f : 0.0f);
    Cmd.Jump = IsHeld(Action::Jump);
    Cmd.Crouch = IsHeld(Action::Crouch);
    Cmd.Punch = IsHeld(Action::Punch);
    Cmd.Kick = IsHeld(Action::Kick);
    Cmd.Block = IsHeld(Action::Block);
    return Cmd;
}

} // namespace fighter::app
