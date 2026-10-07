#include <catch2/catch_test_macros.hpp>

#include "core/log.hpp"

TEST_CASE("isFirstTime: true once per message, then false", "[core][log]") {
    CHECK(fighter::log::isFirstTime("test message A"));
    CHECK_FALSE(fighter::log::isFirstTime("test message A"));
    CHECK(fighter::log::isFirstTime("test message B"));
    CHECK_FALSE(fighter::log::isFirstTime("test message A"));
}
