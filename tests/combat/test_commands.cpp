#include <catch2/catch_test_macros.hpp>

#include "combat/commands.hpp"

using namespace fighter::combat;

TEST_CASE("Commands: no block button, no block zone", "[combat][commands]") {
    CHECK_FALSE(getBlockZone({.Up = true}, true).has_value());
    CHECK_FALSE(getBlockZone({.Down = true}, false).has_value());
}

TEST_CASE("Commands: block alone or with a horizontal direction is mid", "[combat][commands]") {
    CHECK(getBlockZone({.Block = true}, true) == BlockZone::Mid);
    CHECK(getBlockZone({.MoveX = 1.0f, .Block = true}, true) == BlockZone::Mid);
    CHECK(getBlockZone({.MoveX = -1.0f, .Block = true}, true) == BlockZone::Mid);
}

TEST_CASE("Commands: block with up is high, with up-forward mid", "[combat][commands]") {
    CHECK(getBlockZone({.Up = true, .Block = true}, true) == BlockZone::High);
    // Up-back stays high: only forward turns it into the mid zone.
    CHECK(getBlockZone({.MoveX = -1.0f, .Up = true, .Block = true}, true) == BlockZone::High);
    CHECK(getBlockZone({.MoveX = 1.0f, .Up = true, .Block = true}, true) == BlockZone::Mid);
    // Forward depends on the facing: for a fighter facing left it is to the left.
    CHECK(getBlockZone({.MoveX = 1.0f, .Up = true, .Block = true}, false) == BlockZone::High);
    CHECK(getBlockZone({.MoveX = -1.0f, .Up = true, .Block = true}, false) == BlockZone::Mid);
}

TEST_CASE("Commands: block with down is low in any direction", "[combat][commands]") {
    CHECK(getBlockZone({.Down = true, .Block = true}, true) == BlockZone::Low);
    CHECK(getBlockZone({.MoveX = 1.0f, .Down = true, .Block = true}, true) == BlockZone::Low);
    CHECK(getBlockZone({.MoveX = -1.0f, .Up = true, .Down = true, .Block = true}, false) == BlockZone::Low);
}

TEST_CASE("Commands: down crouches unless blocking", "[combat][commands]") {
    CHECK(isCrouching({.Down = true}));
    CHECK_FALSE(isCrouching({.Down = true, .Block = true}));
    CHECK_FALSE(isCrouching({.Up = true}));
}
