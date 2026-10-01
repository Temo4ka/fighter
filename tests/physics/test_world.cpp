#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "physics/world.hpp"

using fighter::physics::World;
using Catch::Approx;

TEST_CASE("physics::World: create, step and gravity", "[physics]") {
    World W({.Gravity = {0.0f, -9.81f}, .SubSteps = 4});
    REQUIRE(W.isValid());
    CHECK(W.getGravity().Y == Approx(-9.81f));
    CHECK(W.getBodyCount() == 0);
    for (int I = 0; I < 10; ++I) W.step(1.0f / 60.0f);
}

TEST_CASE("physics::World: move transfers ownership", "[physics]") {
    World A;
    World B = std::move(A);
    CHECK_FALSE(A.isValid());
    CHECK(B.isValid());

    World C;
    C = std::move(B);   // the old world of C is destroyed, the new one moves in
    CHECK(C.isValid());
    CHECK_FALSE(B.isValid());
}
