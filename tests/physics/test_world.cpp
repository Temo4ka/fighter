#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

TEST_CASE("physics::Body: a kinematic body moves exactly to its target", "[physics]") {
    World PhysWorld;
    Body Slider = PhysWorld.createBody({.Type = BodyType::Kinematic, .Position = {0.0f, 1.0f}});
    PhysWorld.addShape(Slider, {.Kind = ShapeKind::Box, .HalfExtents = {0.1f, 0.1f}});
    CHECK(Slider.getType() == BodyType::Kinematic);

    constexpr float Dt = 1.0f / 60.0f;
    Slider.moveTo({0.3f, 1.2f}, 0.2f, Dt);
    PhysWorld.step(Dt);
    CHECK(Slider.getPosition().X == Approx(0.3f).margin(1e-4f));
    CHECK(Slider.getPosition().Y == Approx(1.2f).margin(1e-4f));   // gravity does not act on it
    // Box2D integrates rotation approximately: a large turn in one step is off
    // slightly, and the next moveTo() corrects it.
    CHECK(Slider.getAngle() == Approx(0.2f).margin(0.005f));

    // Staying in place stops it, however slow the last correction is.
    Slider.moveTo(Slider.getPosition(), Slider.getAngle(), Dt);
    PhysWorld.step(Dt);
    CHECK(Slider.getLinearVelocity().getLength() < 1e-3f);
    CHECK(Slider.getPosition().X == Approx(0.3f).margin(1e-4f));
}

TEST_CASE("physics::World: a body keeps its mass through a kinematic phase", "[physics]") {
    World PhysWorld;
    Body Box = PhysWorld.createBody({.Position = {0.0f, 1.0f}});
    PhysWorld.addShape(Box, {.Kind = ShapeKind::Box, .HalfExtents = {0.2f, 0.2f}});
    Box.setMass(6.0f);

    PhysWorld.setBodyType(Box, BodyType::Kinematic);
    CHECK(Box.getType() == BodyType::Kinematic);
    CHECK(Box.getMass() == 0.0f);
    PhysWorld.setBodyType(Box, BodyType::Dynamic);
    CHECK(Box.getMass() == Approx(6.0f));
}

TEST_CASE("physics::World: a kinematic part hitting a dynamic one is a hit", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    // Fighter 0 strikes with a kinematic ball of 2 kg at 4 m/s.
    Body Striker = addBall(PhysWorld, 0, {0.0f, 1.0f}, {});
    Striker.setMass(2.0f);
    PhysWorld.setBodyType(Striker, BodyType::Kinematic);
    const Body Target = addBall(PhysWorld, 1, {0.5f, 1.0f}, {});

    std::vector<HitEvent> Hits;
    constexpr float Dt = 1.0f / 60.0f;
    for (int Step = 1; Step <= 30; ++Step) {
        Striker.moveTo({4.0f * Dt * static_cast<float>(Step), 1.0f}, 0.0f, Dt);
        PhysWorld.step(Dt);
        std::ranges::copy(PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
    REQUIRE_FALSE(Hits.empty());
    CHECK(Hits[0].Attacker.Fighter == 0);
    CHECK(Hits[0].ApproachSpeed == Approx(4.0f).margin(0.5f));
    // The impulse of two free balls of 2 and 5 kg, not of an immovable striker.
    const float ReducedMass = 2.0f * 5.0f / 7.0f;
    CHECK(Hits[0].Impulse == Approx(Hits[0].ApproachSpeed * ReducedMass).epsilon(0.01));
    CHECK(Target.getLinearVelocity().X > 0.0f);   // the kinematic striker pushed it
}

TEST_CASE("physics::World: a kinematic part strikes with its strike mass", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    Body Striker = addBall(PhysWorld, 0, {0.0f, 1.0f}, {});
    PhysWorld.setBodyType(Striker, BodyType::Kinematic);
    PhysWorld.setStrikeMass(Striker, 20.0f);   // a light fist on a heavy arm
    addBall(PhysWorld, 1, {0.5f, 1.0f}, {});

    std::vector<HitEvent> Hits;
    constexpr float Dt = 1.0f / 60.0f;
    for (int Step = 1; Step <= 30 && Hits.empty(); ++Step) {
        Striker.moveTo({4.0f * Dt * static_cast<float>(Step), 1.0f}, 0.0f, Dt);
        PhysWorld.step(Dt);
        std::ranges::copy(PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
    REQUIRE(Hits.size() == 1);
    CHECK(Hits[0].Impulse == Approx(Hits[0].ApproachSpeed * 20.0f * 5.0f / 25.0f).epsilon(0.01));
}

TEST_CASE("physics::World: kinematic parts of different fighters hit each other", "[physics]") {
    // Box2D does not collide two kinematic bodies; the world checks them.
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    Body Shin = addBall(PhysWorld, 0, {0.0f, 0.3f}, {});
    Shin.setMass(4.0f);
    PhysWorld.setBodyType(Shin, BodyType::Kinematic);
    Body Leg = addBall(PhysWorld, 1, {0.5f, 0.3f}, {});
    PhysWorld.setBodyType(Leg, BodyType::Kinematic);
    // A posed part of the same fighter is never hit.
    Body Own = addBall(PhysWorld, 0, {0.5f, 0.6f}, {});
    PhysWorld.setBodyType(Own, BodyType::Kinematic);

    std::vector<HitEvent> Hits;
    constexpr float Dt = 1.0f / 60.0f;
    for (int Step = 1; Step <= 20; ++Step) {
        Shin.moveTo({3.0f * Dt * static_cast<float>(Step), 0.3f}, 0.0f, Dt);
        Own.moveTo({0.5f, 0.6f}, 0.0f, Dt);
        PhysWorld.step(Dt);
        std::ranges::copy(PhysWorld.getHitEvents(), std::back_inserter(Hits));
    }
    // One hit when the contact begins, none while it lasts.
    REQUIRE(Hits.size() == 1);
    CHECK(Hits[0].Attacker.Fighter == 0);
    CHECK(Hits[0].Victim.Fighter == 1);
    CHECK(Hits[0].ApproachSpeed == Approx(3.0f).margin(0.1f));
    CHECK(Hits[0].Impulse == Approx(Hits[0].ApproachSpeed * 4.0f * 5.0f / 9.0f).epsilon(0.01));
    CHECK(Hits[0].Point.X == Approx(Leg.getPosition().X - 0.1f).margin(0.06f));
    CHECK(Hits[0].Point.Y == Approx(0.3f).margin(0.01f));
    // Nothing pushes posed parts: the leg stays where the code put it.
    CHECK(Leg.getPosition().X == Approx(0.5f));
}

TEST_CASE("physics::World: touching and overlapping another fighter", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}});
    const Body Fist = addBall(PhysWorld, 0, {0.0f, 1.0f}, {});
    const Body Chest = addBall(PhysWorld, 1, {0.15f, 1.0f}, {});
    const Body Far = addBall(PhysWorld, 1, {3.0f, 1.0f}, {});
    PhysWorld.step(1.0f / 60.0f);
    CHECK(PhysWorld.isTouchingOtherFighter(Fist));
    CHECK(PhysWorld.isOverlappingOtherFighter(Fist));
    CHECK_FALSE(PhysWorld.isTouchingOtherFighter(Far));
    CHECK_FALSE(PhysWorld.isOverlappingOtherFighter(Far));

    // A filtered pair does not touch, but still overlaps.
    Body Ghost = addBall(PhysWorld, 0, {3.1f, 1.0f}, {});
    Ghost.setCollisionMask(0);
    PhysWorld.step(1.0f / 60.0f);
    CHECK_FALSE(PhysWorld.isTouchingOtherFighter(Ghost));
    CHECK(PhysWorld.isOverlappingOtherFighter(Ghost));
    CHECK(PhysWorld.isOverlappingOtherFighter(Chest));
}

TEST_CASE("physics::World: mirroring shapes and joints", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}});
    Body Upper = PhysWorld.createBody({.Position = {0.0f, 1.0f}});
    PhysWorld.addShape(Upper, {.Kind = ShapeKind::Box, .Center = {0.1f, 0.0f}, .HalfExtents = {0.2f, 0.05f}});
    Upper.setMass(3.0f);
    Body Lower = PhysWorld.createBody({.Position = {0.0f, 0.6f}});
    PhysWorld.addShape(Lower, {.Kind = ShapeKind::Capsule, .Begin = {0.05f, 0.1f}, .End = {0.1f, -0.2f},
                               .Radius = 0.04f});
    Lower.setMass(2.0f);
    const RevoluteJoint Hinge = PhysWorld.createRevoluteJoint({
        .BodyA = Upper, .BodyB = Lower, .Anchor = {0.05f, 0.75f}, .LowerAngle = -0.2f, .UpperAngle = 1.0f,
        .MaxMotorTorque = 5.0f,
    });
    CHECK(Upper.getWorldCenterOfMass().X == Approx(0.1f).margin(1e-4f));

    PhysWorld.mirrorShapes(Upper);
    PhysWorld.mirrorShapes(Lower);
    CHECK(Upper.getMass() == Approx(3.0f));
    CHECK(Upper.getWorldCenterOfMass().X == Approx(-0.1f).margin(1e-4f));
    const RevoluteJoint Mirrored = PhysWorld.mirrorJoint(Hinge);
    CHECK_FALSE(Hinge.isValid());
    REQUIRE(Mirrored.isValid());
    CHECK(Mirrored.getLowerLimit() == Approx(-1.0f));
    CHECK(Mirrored.getUpperLimit() == Approx(0.2f));
    CHECK(Mirrored.getMaxMotorTorque() == Approx(5.0f));
    CHECK(Mirrored.getAnchor().X == Approx(-0.05f).margin(1e-4f));
    CHECK(Mirrored.getAngle() == Approx(0.0f).margin(1e-4f));
    // The hinge holds the mirrored bodies where they are.
    for (int Step = 0; Step < 30; ++Step) PhysWorld.step(1.0f / 60.0f);
    CHECK(Mirrored.getAnchor().X == Approx(-0.05f).margin(1e-3f));
    CHECK(Lower.getPosition().Y == Approx(0.6f).margin(1e-3f));
}

TEST_CASE("physics::World: a fast posed hit takes its normal from before the step", "[physics]") {
    // A posed ball 0.2 m per step towards another one a little higher: after
    // the step the line between the centers is steep, before it was not.
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    constexpr float Dt = 1.0f / 60.0f;
    constexpr float Speed = 12.0f;
    Body Shin = addBall(PhysWorld, 0, {0.0f, 0.0f}, {});
    PhysWorld.setBodyType(Shin, BodyType::Kinematic);
    Body Leg = addBall(PhysWorld, 1, {0.35f, 0.12f}, {});
    PhysWorld.setBodyType(Leg, BodyType::Kinematic);

    Shin.moveTo({Speed * Dt, 0.0f}, 0.0f, Dt);
    PhysWorld.step(Dt);
    const auto Hits = PhysWorld.getHitEvents();
    REQUIRE(Hits.size() == 1);
    const Vec2 Before = Vec2{0.35f, 0.12f}.getNormalized();
    CHECK(Hits[0].ApproachSpeed == Approx(Speed * Before.X).epsilon(0.02));
}

TEST_CASE("physics::World: how deep a posed part overlaps the other fighter's posed parts", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}});
    Body Shin = addBall(PhysWorld, 0, {0.0f, 0.0f}, {});
    PhysWorld.setBodyType(Shin, BodyType::Kinematic);
    Body Leg = addBall(PhysWorld, 1, {0.15f, 0.0f}, {});
    PhysWorld.setBodyType(Leg, BodyType::Kinematic);
    // Radii 0.1 + 0.1, centers 0.15 apart.
    CHECK(PhysWorld.getPosedPenetration(Shin) == Approx(0.05f).margin(1e-4f));
    CHECK(PhysWorld.getPosedPenetration(Leg) == Approx(0.05f).margin(1e-4f));
    // Own posed parts and dynamic parts of the other fighter do not count.
    Body Own = addBall(PhysWorld, 0, {0.0f, 0.1f}, {});
    PhysWorld.setBodyType(Own, BodyType::Kinematic);
    addBall(PhysWorld, 1, {-0.1f, 0.0f}, {});
    CHECK(PhysWorld.getPosedPenetration(Shin) == Approx(0.05f).margin(1e-4f));
    Leg.setTransform({0.5f, 0.0f}, 0.0f);
    CHECK(PhysWorld.getPosedPenetration(Shin) == 0.0f);
}

TEST_CASE("physics::World: a posed part goes back to where it met the other fighter", "[physics]") {
    World PhysWorld({.Gravity = {0.0f, 0.0f}, .HitSpeedThreshold = 1.0f});
    constexpr float Dt = 1.0f / 60.0f;
    constexpr float Depth = 0.01f;
    Body Shin = addBall(PhysWorld, 0, {0.0f, 0.0f}, {});
    PhysWorld.setBodyType(Shin, BodyType::Kinematic);
    Body Leg = addBall(PhysWorld, 1, {0.3f, 0.0f}, {});
    PhysWorld.setBodyType(Leg, BodyType::Kinematic);

    // 0.2 m in one step: from 0.1 m apart to 0.1 m deep.
    Shin.moveTo({0.2f, 0.0f}, 0.4f, Dt);
    PhysWorld.step(Dt);
    REQUIRE(PhysWorld.getHitEvents().size() == 1);   // the hit has the speed of the whole step
    const float TurnedTo = Shin.getAngle();   // about 0.4 (Box2D integrates the rotation)
    const std::array Strikers = {Shin};
    const float Kept = PhysWorld.findPosedStop(Strikers, Depth, false).value_or(-1.0f);
    CHECK(Kept == Approx((0.1f + Depth) / 0.2f).margin(1e-4f));
    PhysWorld.rewindBody(Shin, Kept);
    CHECK(Shin.getPosition().X == Approx(0.11f).margin(1e-4f));
    // b2MakeRot approximates the sine and cosine: a few mrad.
    CHECK(Shin.getAngle() == Approx(TurnedTo * Kept).margin(0.005f));
    CHECK(PhysWorld.getPosedPenetration(Shin) == Approx(Depth).margin(1e-4f));

    // Pressed further, a part held at the contact stays there; one that is
    // not held passes: the contact began before this step.
    Shin.moveTo({0.25f, 0.0f}, 0.0f, Dt);
    PhysWorld.step(Dt);
    CHECK(PhysWorld.findPosedStop(Strikers, Depth, true).value_or(-1.0f) == Approx(0.0f).margin(1e-5f));
    CHECK_FALSE(PhysWorld.findPosedStop(Strikers, Depth, false).has_value());
    PhysWorld.rewindBody(Shin, 0.0f);
    CHECK(PhysWorld.getPosedPenetration(Shin) == Approx(Depth).margin(1e-5f));   // exactly where it was

    // Pulled back, it moves on: no contact.
    Shin.moveTo({0.05f, 0.0f}, 0.0f, Dt);
    PhysWorld.step(Dt);
    CHECK_FALSE(PhysWorld.findPosedStop(Strikers, Depth, true).has_value());

    // A new contact no deeper than the limit is a contact that needs no stop.
    Shin.moveTo({0.105f, 0.0f}, 0.0f, Dt);
    PhysWorld.step(Dt);
    CHECK(PhysWorld.findPosedStop(Strikers, Depth, false) == 1.0f);

    // A part that was deep inside before the step is left alone even when
    // held: there is no contact to go back to.
    Shin.setTransform({0.2f, 0.0f}, 0.0f);
    PhysWorld.step(Dt);
    Shin.moveTo({0.25f, 0.0f}, 0.0f, Dt);
    PhysWorld.step(Dt);
    CHECK_FALSE(PhysWorld.findPosedStop(Strikers, Depth, true).has_value());
}
