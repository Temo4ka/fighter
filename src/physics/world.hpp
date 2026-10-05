//===- physics/world.hpp - RAII wrapper over a Box2D world ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares physics::World, which owns a Box2D world with its
/// bodies, shapes and joints, steps it and reports hits between body parts.
///
/// Box2D types (b2WorldId, b2Vec2, ...) never leave src/physics/
/// (docs/DEVELOPMENT_PLAN.md, section 3.1): the rest of the code sees only our
/// types, so the engine can be replaced without touching the combat logic.
///
/// No user callbacks run inside step(): hits are collected after the Box2D
/// step and read with getHitEvents(), which keeps the order of work explicit
/// and the simulation deterministic.
///
/// Body parts may be kinematic (moved by code, see Body::moveTo). The solver
/// treats them as infinitely heavy, so the contact impulse of a hit that
/// involves one says nothing about the strike; such hits report the impulse
/// of the same collision between free bodies with the parts' masses instead
/// (for a kinematic part, the mass of the limb it strikes with).
///
/// Box2D does not collide two kinematic bodies at all, so the world checks
/// the kinematic parts of different fighters against each other itself
/// after every step: a posed leg that starts touching the opponent's posed
/// leg or pelvis fast enough is a hit like any other (a low kick). Nothing
/// pushes back: both bodies are moved by code. A fast limb moves far in one
/// step, so the normal of such a hit is taken from where the two parts were
/// closest before the step, when they were still apart. Nothing stops a posed
/// limb either: the code that poses it asks findPosedStop() how far along its
/// motion of the step it could go without sinking into the opponent and moves
/// it back there with rewindBody() (a kick stops at the leg it hits, or at a
/// torso the solver cannot push out of its way).
///
/// A posed limb is fast enough to sink into a dynamic part in one step,
/// before Box2D has seen the contact (it creates contacts from where bodies
/// were at the start of a step). Such a new overlap is a hit too, found the
/// same way as a hit between posed parts.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/vec2.hpp"
#include "physics/body.hpp"
#include "physics/events.hpp"
#include "physics/joint.hpp"

namespace fighter::physics {

class World {
public:
    struct Config {
        Vec2 Gravity{0.0f, -9.81f};   ///< m/s^2, Y up.
        int SubSteps = 4;             ///< Box2D solver substeps per simulation step.
        /// Closing speed above which a contact between body parts of
        /// different fighters is reported as a hit, m/s.
        float HitSpeedThreshold = 1.0f;
    };

    World() : World(Config{}) {}
    explicit World(Config Settings);
    ~World();

    /// The world owns Box2D resources: it can be moved but not copied.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&& Other) noexcept;
    World& operator=(World&& Other) noexcept;

    /// \name Construction
    /// Bodies, shapes and joints live as long as the world.
    /// @{
    Body createBody(const BodyDef& Def);
    void addShape(Body Target, const ShapeDef& Shape);
    RevoluteJoint createRevoluteJoint(const RevoluteJointDef& Def);
    /// @}

    /// Switches a body between kinematic and dynamic. Set the mass of a body
    /// part while it is dynamic: a kinematic body has no mass in the solver,
    /// and the world keeps the last dynamic mass for the impulse of hits.
    void setBodyType(Body Target, BodyType Type);
    /// The mass a body part hits with while it is kinematic, kg: a posed limb
    /// strikes with the limb behind it, not only with the touching part. By
    /// default it is the part's own last dynamic mass.
    void setStrikeMass(Body Target, float Kg);

    /// Advances the simulation by \p Dt and collects the hits of this step.
    void step(float Dt);

    /// \name Contacts between fighters
    /// @{
    /// Does \p Target touch a body part of another fighter (a contact the
    /// solver resolves)?
    bool isTouchingOtherFighter(Body Target) const;
    /// Does a shape of \p Target overlap a body part of another fighter,
    /// whatever their collision filters say?
    bool isOverlappingOtherFighter(Body Target) const;
    /// Would \p Target, placed with its origin at \p Position and turned by
    /// \p Angle, come closer than \p Margin (m) to a body part of another
    /// fighter, whatever their collision filters say? Nothing moves.
    bool isOverlappingOtherFighterAt(Body Target, Vec2 Position, float Angle, float Margin) const;
    /// The deepest overlap between any body parts of different fighters,
    /// whatever their types and collision filters: the measure of "nothing
    /// passes through the opponent". Nullopt if no two parts overlap.
    std::optional<PartOverlap> findDeepestOverlap() const;
    /// @}

    /// \name Posed parts of different fighters
    /// Box2D does not collide two kinematic bodies; these let the code that
    /// poses them keep them from sinking into each other.
    /// @{
    /// How deep the shapes of \p Target overlap the posed (kinematic) parts
    /// of other fighters, m; 0 if they do not touch. Only for a kinematic
    /// \p Target; the solver keeps dynamic parts out by itself.
    float getPosedPenetration(Body Target) const;
    /// Do the posed bodies \p Strikers run into a part of another fighter in
    /// the last step, and how much of their motion in the step could they
    /// have made with none of them sinking deeper than \p MaxDepth (m) into
    /// it, or than it was before the step (it stays where it is now)?
    /// Nullopt: no contact; 1: a contact that needs no stop; 0: the other
    /// part moved into them.
    /// Only closing contacts count: the striker moved towards the other part
    /// before the step (a contact it slides along or leaves is no stop). A
    /// posed part of the opponent counts when the striker touches it, a
    /// dynamic one only when the striker sank too deep into it: the solver
    /// did not push it out of the way (it is held by its joints, or it
    /// ignores posed parts, as a knocked-down body does). A kinematic body
    /// moves at a constant velocity during a step, so the share is found by
    /// bisection along that straight motion.
    std::optional<float> findPosedStop(std::span<const Body> Strikers, float MaxDepth) const;
    /// The smallest gap between the shapes of \p First and \p Second, their
    /// bodies placed at the given origins and angles, m; negative: how deep
    /// they overlap. Nothing moves.
    float getGapAt(Body First, Vec2 FirstPosition, float FirstAngle, Body Second, Vec2 SecondPosition,
                   float SecondAngle) const;
    /// Puts \p Target back along its motion of the last step: \p Fraction 0
    /// is where it was before the step, 1 is where it is now. Velocities do
    /// not change.
    void rewindBody(Body Target, float Fraction);
    /// @}

    /// \name Mirroring (turning a fighter around)
    /// @{
    /// Mirrors every shape of \p Target about the body's local Y axis. The
    /// mass, the material and the filters stay.
    void mirrorShapes(Body Target);
    /// Replaces \p Joint by its mirror image: the local anchors mirrored
    /// about the bodies' local Y axes, the reference angle negated, the
    /// limits swapped and negated; the motor keeps its settings. Returns the
    /// new joint; \p Joint becomes invalid.
    RevoluteJoint mirrorJoint(RevoluteJoint Joint);
    /// @}

    /// Hits between body parts of different fighters during the last step,
    /// in a deterministic order.
    std::span<const HitEvent> getHitEvents() const { return Hits; }

    /// Routes Box2D debug drawing into debug::draw*: shapes go to Hurtbox
    /// (colored by fighter) or Static, joints to Joints, contact points and
    /// normals to Contacts. Does nothing in the release build.
    void drawDebug() const;

    Vec2 getGravity() const;
    int getBodyCount() const;
    int getJointCount() const;
    bool isValid() const { return Id != 0; }

private:
    /// A body that belongs to a fighter, with its state before the last step:
    /// after the step the velocities already include the hit itself.
    struct PartBody {
        Body Handle;
        PartRef Part;
        Vec2 CenterBeforeStep;
        Vec2 PositionBeforeStep;          ///< Body origin.
        float AngleBeforeStep = 0.0f;
        Vec2 RotationBeforeStep{1.0f, 0.0f};   ///< (cos, sin) of AngleBeforeStep, exact.
        Vec2 VelocityBeforeStep;
        float AngularVelocityBeforeStep = 0.0f;
        /// Mass of the body while it was last dynamic, kg.
        float DynamicMass = 0.0f;
        /// setStrikeMass(); 0 means DynamicMass.
        float StrikeMass = 0.0f;
    };

    /// A body placement: the origin and the rotation as (cos, sin), kept
    /// exactly as Box2D stores it (an angle would not survive the round
    /// trip unchanged).
    struct Placement {
        Vec2 Position;
        Vec2 Rotation{1.0f, 0.0f};
    };

    /// A pair of kinematic part bodies (PartBodies slots, First < Second)
    /// that touched after a step.
    using SlotPair = std::pair<uint32_t, uint32_t>;

    void destroy();
    void recordPartVelocities();
    void collectHits();
    /// Hits between the kinematic parts of different fighters, which Box2D
    /// does not collide.
    void collectPosedHits();
    /// Hits of kinematic parts that sank into a dynamic part of another
    /// fighter in the step, before Box2D had a contact for them.
    void collectTunnelHits();
    void addHit(const PartBody& PartA, const PartBody& PartB, Vec2 Point, Vec2 Normal, float ApproachSpeed,
                float Impulse);
    Vec2 getVelocityBeforeStep(const PartBody& Entry, Vec2 WorldPoint) const;
    /// The slot of a body part of a fighter, if \p Target is one.
    std::optional<uint32_t> findSlot(Body Target) const;
    /// How deep \p Entry, its body placed at \p Placed, overlaps the
    /// posed parts of other fighters, m; 0 if it does not.
    float measurePosedPenetration(const PartBody& Entry, const Placement& Placed) const;
    /// How deep \p Entry at \p Placed overlaps \p Other at \p OtherPlaced,
    /// m; negative: how far apart they are.
    float measurePairPenetration(const PartBody& Entry, const Placement& Placed, const PartBody& Other,
                                 const Placement& OtherPlaced) const;
    /// Where \p Entry was at \p Fraction of the last step (1 is now).
    Placement getPlacementDuringStep(const PartBody& Entry, float Fraction) const;
    /// Mass of a part for the impulse of a hit: its strike mass if it is
    /// kinematic now, kg.
    float getStrikeMass(const PartBody& Entry) const;

    uint32_t Id = 0;   ///< b2WorldId packed with b2StoreWorldId; 0 is null.
    int SubSteps = 4;
    std::vector<PartBody> PartBodies;
    std::vector<HitEvent> Hits;
    float HitSpeedThreshold = 1.0f;
    std::vector<SlotPair> TouchingPosed;   ///< Sorted.
};

} // namespace fighter::physics
