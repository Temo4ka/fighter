#include "app/input.hpp"

namespace fighter::app {

InputSystem::InputSystem(std::vector<Binding> bindings) : bindings_(std::move(bindings)) {}

std::vector<Binding> InputSystem::defaultBindings() {
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

bool InputSystem::onKey(sf::Keyboard::Scancode key, bool pressed) {
    bool bound = false;
    for (const Binding& b : bindings_) {
        if (b.key != key || b.player < 0 || b.player >= kPlayers) continue;
        held_[static_cast<std::size_t>(b.player)][static_cast<std::size_t>(b.action)] = pressed;
        bound = true;
    }
    return bound;
}

void InputSystem::reset() {
    held_ = {};
}

combat::PlayerCommands InputSystem::commands(int player) const {
    if (player < 0 || player >= kPlayers) return {};
    const ActionState& s = held_[static_cast<std::size_t>(player)];
    auto is = [&](Action a) { return s[static_cast<std::size_t>(a)]; };

    combat::PlayerCommands c;
    // Обе стороны сразу — стоим на месте.
    c.moveX = (is(Action::Right) ? 1.0f : 0.0f) - (is(Action::Left) ? 1.0f : 0.0f);
    c.jump = is(Action::Jump);
    c.crouch = is(Action::Crouch);
    c.punch = is(Action::Punch);
    c.kick = is(Action::Kick);
    c.block = is(Action::Block);
    return c;
}

} // namespace fighter::app
