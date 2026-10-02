#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "physics/world.hpp"

using namespace fighter;
using namespace fighter::physics;

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

namespace {

/// A ball of fighter \p Fighter (its head) at \p Position moving with \p Velocity.
Body addBall(World& PhysWorld, uint8_t Fighter, Vec2 Position, Vec2 Velocity) {
    Body Ball = PhysWorld.createBody({.Position = Position, .Part = PartRef{Fighter, BodyPart::Head}});
    PhysWorld.addShape(Ball, {.Kind = ShapeKind::Circle, .Radius = 0.1f, .CollisionGroup = -(Fighter + 1),
                              .EnableHitEvents = true});
    Ball.setMass(5.0f);
    Ball.setLinearVelocity(Velocity);
    return Ball;
}

void stepFor(World& PhysWorld, int Steps, std::vector<HitEvent>& Hits) {
    for (int Step = 0; Step < Steps; ++Step) {
        PhysWorld.step(1.0f / 60.0f);
        std::ranges::copy(PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
}

} // namespace

TEST_CASE("physics::Body: mass, position and velocity", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}});
    Body Box = PhysWorld.createBody({.Position = {1.0f, 2.0f}});
    PhysWorld.addShape(Box, {.Kind = ShapeKind::Box, .HalfExtents = {0.5f, 0.25f}});
    Box.setMass(7.5f);
    Box.setLinearVelocity({2.0f, 0.0f});
    CHECK(Box.isValid());
    CHECK(Box.getMass() == Approx(7.5f));
    CHECK(Box.getPosition() == Vec2{1.0f, 2.0f});

    PhysWorld.step(0.5f);
    CHECK(Box.getPosition().X == Approx(2.0f));
    CHECK(Box.getWorldPoint({0.5f, 0.0f}).X == Approx(2.5f));
}

TEST_CASE("physics::RevoluteJoint: the limits hold", "[physics]") {
    World PhysWorld;   // gravity pulls the arm down past its lower limit
    const Body Anchor = PhysWorld.createBody({.Type = BodyType::Static});
    Body Arm = PhysWorld.createBody({.Position = {0.5f, 0.0f}});
    PhysWorld.addShape(Arm, {.Kind = ShapeKind::Capsule, .Begin = {-0.5f, 0.0f}, .End = {0.5f, 0.0f}, .Radius = 0.05f});
    const RevoluteJoint Hinge = PhysWorld.createRevoluteJoint({
        .BodyA = Anchor, .BodyB = Arm, .Anchor = {0.0f, 0.0f}, .LowerAngle = -0.5f, .UpperAngle = 0.5f,
        .EnableMotor = false,
    });
    for (int Step = 0; Step < 120; ++Step) PhysWorld.step(1.0f / 60.0f);
    CHECK(Hinge.isValid());
    CHECK(Hinge.getAngle() == Approx(-0.5f).margin(0.02f));
    CHECK(Hinge.getLowerLimit() == Approx(-0.5f));
    CHECK(Arm.getAngle() == Approx(-0.5f).margin(0.02f));
}

TEST_CASE("physics::World: a fast contact between fighters is a hit", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    addBall(PhysWorld, 0, {0.0f, 1.0f}, {5.0f, 0.0f});
    addBall(PhysWorld, 1, {0.5f, 1.0f}, {});

    std::vector<HitEvent> Hits;
    stepFor(PhysWorld, 30, Hits);
    REQUIRE(Hits.size() == 1);
    CHECK(Hits[0].Attacker.Fighter == 0);   // the moving ball
    CHECK(Hits[0].Victim.Fighter == 1);
    CHECK(Hits[0].Victim.Part == BodyPart::Head);
    CHECK(Hits[0].ApproachSpeed == Approx(5.0f).margin(0.5f));
    CHECK(Hits[0].Impulse > 0.0f);
    // The contact point is on the line between the balls.
    CHECK(Hits[0].Point.Y == Approx(1.0f).margin(0.01f));
    CHECK(Hits[0].Point.X > 0.1f);
    CHECK(Hits[0].Point.X < 0.6f);
}

TEST_CASE("physics::World: slow contacts and parts of one fighter are not hits", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    // Slow: below the threshold.
    addBall(PhysWorld, 0, {0.0f, 1.0f}, {0.5f, 0.0f});
    addBall(PhysWorld, 1, {0.4f, 1.0f}, {});
    // Fast, but both parts belong to fighter 0: they do not even collide.
    const Body Through = addBall(PhysWorld, 0, {0.0f, 3.0f}, {5.0f, 0.0f});
    addBall(PhysWorld, 0, {0.5f, 3.0f}, {});

    std::vector<HitEvent> Hits;
    stepFor(PhysWorld, 30, Hits);
    CHECK(Hits.empty());
    CHECK(Through.getPosition().X > 2.0f);   // passed through its own fighter's part
}
