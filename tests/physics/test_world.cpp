#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "physics/world.hpp"

using fighter::physics::World;
using Catch::Approx;

TEST_CASE("physics::World: create, step and gravity", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, -9.81f}, .SubSteps = 4});
    REQUIRE(PhysWorld.isValid());
    CHECK(PhysWorld.getGravity().Y == Approx(-9.81f));
    CHECK(PhysWorld.getBodyCount() == 0);
    for (int Step = 0; Step < 10; ++Step) PhysWorld.step(1.0f / 60.0f);
}

TEST_CASE("physics::World: move transfers ownership", "[physics]") {
    World First;
    World Second = std::move(First);
    CHECK_FALSE(First.isValid());
    CHECK(Second.isValid());

    World Third;
    Third = std::move(Second);   // the old world of Third is destroyed, the new one moves in
    CHECK(Third.isValid());
    CHECK_FALSE(Second.isValid());
}
