#include <catch2/catch_test_macros.hpp>

#include "app/input.hpp"

using fighter::app::InputSystem;
using Scan = sf::Keyboard::Scan;

TEST_CASE("InputSystem: players have separate keys", "[app][input]") {
    InputSystem input;
    input.onKey(Scan::D, true);
    input.onKey(Scan::Left, true);
    CHECK(input.commands(0).moveX == 1.0f);
    CHECK(input.commands(1).moveX == -1.0f);
}

TEST_CASE("InputSystem: key stays held until released", "[app][input]") {
    InputSystem input;
    input.onKey(Scan::W, true);
    CHECK(input.commands(0).jump);
    CHECK(input.commands(0).jump);   // повторный опрос — всё ещё нажата
    input.onKey(Scan::W, false);
    CHECK_FALSE(input.commands(0).jump);
}

TEST_CASE("InputSystem: left and right together cancel out", "[app][input]") {
    InputSystem input;
    input.onKey(Scan::A, true);
    input.onKey(Scan::D, true);
    CHECK(input.commands(0).moveX == 0.0f);
}

TEST_CASE("InputSystem: reset releases everything", "[app][input]") {
    InputSystem input;
    input.onKey(Scan::F, true);
    input.onKey(Scan::K, true);
    input.reset();
    CHECK(input.commands(0) == fighter::combat::PlayerCommands{});
    CHECK(input.commands(1) == fighter::combat::PlayerCommands{});
}

TEST_CASE("InputSystem: unbound key does not affect players", "[app][input]") {
    InputSystem input;
    CHECK_FALSE(input.onKey(Scan::Z, true));
    CHECK(input.commands(0) == fighter::combat::PlayerCommands{});
}
