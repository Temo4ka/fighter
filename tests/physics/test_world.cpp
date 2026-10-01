#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "physics/world.hpp"

using fighter::physics::World;
using Catch::Approx;

TEST_CASE("physics::World: create, step and gravity", "[physics]") {
    World world({.gravity = {0.0f, -9.81f}, .subSteps = 4});
    REQUIRE(world.valid());
    CHECK(world.gravity().y == Approx(-9.81f));
    CHECK(world.bodyCount() == 0);
    for (int i = 0; i < 10; ++i) world.step(1.0f / 60.0f);
}

TEST_CASE("physics::World: move transfers ownership", "[physics]") {
    World a;
    World b = std::move(a);
    CHECK_FALSE(a.valid());
    CHECK(b.valid());

    World c;
    c = std::move(b);   // старый мир c уничтожается, новый переезжает
    CHECK(c.valid());
    CHECK_FALSE(b.valid());
}
