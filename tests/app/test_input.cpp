#include <catch2/catch_test_macros.hpp>

#include "app/input.hpp"

using fighter::app::InputSystem;
using fighter::combat::PlayerCommands;
using Scan = sf::Keyboard::Scan;

TEST_CASE("InputSystem: players have separate keys", "[app][input]") {
    InputSystem Input;
    Input.onKey(Scan::D, true);
    Input.onKey(Scan::Left, true);
    CHECK(Input.getCommands(0).MoveX == 1.0f);
    CHECK(Input.getCommands(1).MoveX == -1.0f);
}

TEST_CASE("InputSystem: key stays held until released", "[app][input]") {
    InputSystem Input;
    Input.onKey(Scan::W, true);
    CHECK(Input.getCommands(0).Up);
    CHECK(Input.getCommands(0).Up);   // asking again: still held
    Input.onKey(Scan::W, false);
    CHECK_FALSE(Input.getCommands(0).Up);
}

TEST_CASE("InputSystem: left and right together cancel out", "[app][input]") {
    InputSystem Input;
    Input.onKey(Scan::A, true);
    Input.onKey(Scan::D, true);
    CHECK(Input.getCommands(0).MoveX == 0.0f);
}

TEST_CASE("InputSystem: reset releases everything", "[app][input]") {
    InputSystem Input;
    Input.onKey(Scan::F, true);
    Input.onKey(Scan::K, true);
    Input.reset();
    CHECK(Input.getCommands(0) == PlayerCommands{});
    CHECK(Input.getCommands(1) == PlayerCommands{});
}

TEST_CASE("InputSystem: unbound key does not affect players", "[app][input]") {
    InputSystem Input;
    CHECK_FALSE(Input.onKey(Scan::Z, true));
    CHECK(Input.getCommands(0) == PlayerCommands{});
}

TEST_CASE("InputSystem: default layout gives each player all actions", "[app][input]") {
    InputSystem Input;
    for (const auto Key : {Scan::W, Scan::S, Scan::LShift, Scan::F, Scan::G, Scan::R, Scan::T}) {
        Input.onKey(Key, true);
    }
    for (const auto Key : {Scan::Up, Scan::Down, Scan::RShift, Scan::K, Scan::L, Scan::I, Scan::O}) {
        Input.onKey(Key, true);
    }
    const PlayerCommands All{.Up = true, .Down = true, .Block = true, .Light = true, .Heavy = true,
                             .Kick = true, .Special = true};
    CHECK(Input.getCommands(0) == All);
    CHECK(Input.getCommands(1) == All);
}
