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

#include <bitset>
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
        /// Box2D steps per step(), each over an equal share of its Dt: a
        /// fast limb moves less per Box2D step, so it sinks less deep into
        /// what it hits before the solver sees the contact.
        int StepPasses = 1;
        int SubSteps = 4;             ///< Box2D solver substeps per Box2D step.
        /// Stiffness of the contacts between bodies, Hz: a body pressed into
        /// another one (an arm pushed by its motors) sinks in less the
        /// stiffer they are. Box2D caps it at 1/8 of the substep rate.
        float ContactHertz = 30.0f;
        /// Friction between body parts of different fighters; nullopt: the
        /// frictions of the two shapes mixed as for any contact. A limb
        /// pressed onto the opponent drags it along less the lower it is.
        std::optional<float> FighterFriction;
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
    /// fighter (other than \p Ignored), whatever their collision filters
    /// say? Nothing moves.
    bool isOverlappingOtherFighterAt(Body Target, Vec2 Position, float Angle, float Margin,
                                     const std::bitset<BodyPartCount>& Ignored = {}) const;
    /// The deepest overlap between any body parts of different fighters,
    /// whatever their types and collision filters: the measure of "nothing
    /// passes through the opponent". Nullopt if no two parts overlap.
    std::optional<PartOverlap> findDeepestOverlap() const;
    /// Every pair of body parts of different fighters that overlap now, with
    /// the deepest overlap of their shapes, whatever their types and
    /// collision filters; in a deterministic order.
    std::vector<PartOverlap> findOverlaps() const;
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
    /// Only closing contacts count: the striker's own motion in the step
    /// took it deeper into the other part (a contact it slides along or
    /// leaves, or one the other part moved into, is no stop). With a valid
    /// \p Carrier (the body the strikers hang on, a pelvis) the striker's own
    /// motion is the one relative to the carrier: a strike carried into the
    /// opponent by the whole body is kept apart by the code that moves the
    /// body (rig::keepApart), not stopped here; the share returned is then
    /// the share of that relative motion, the carrier staying where it is. A
    /// posed part of the opponent counts when the striker touches it, a
    /// dynamic one only when the striker sank too deep into it: the solver
    /// did not push it out of the way (it is held by its joints, or pinned
    /// to the floor). Going back may not take a striker deeper into any
    /// other part than it is now; if no share keeps every part shallow
    /// enough, the least deep one is returned. A kinematic body moves at a
    /// constant velocity during a step, so the share is found by bisection
    /// along that straight motion.
    std::optional<float> findPosedStop(std::span<const Body> Strikers, float MaxDepth, Body Carrier = {}) const;
    /// The smallest gap between the shapes of \p First and \p Second, their
    /// bodies placed at the given origins and angles, m; negative: how deep
    /// they overlap. Nothing moves.
    float getGapAt(Body First, Vec2 FirstPosition, float FirstAngle, Body Second, Vec2 SecondPosition,
                   float SecondAngle) const;
    /// Puts \p Target back along its motion of the last step: \p Fraction 0
    /// is where it was before the step, 1 is where it is now. With a valid
    /// \p Carrier only the motion relative to the carrier goes back (as in
    /// findPosedStop()). Velocities do not change.
    void rewindBody(Body Target, float Fraction, Body Carrier = {});
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

    /// A hit between two dynamic parts in the current step: its impulse is
    /// summed over the Box2D steps of the simulation step.
    struct SolvedHit {
        uint64_t ShapeA = 0;   ///< b2ShapeId packed with b2StoreShapeId.
        uint64_t ShapeB = 0;
        size_t HitIndex = 0;   ///< Into Hits.
    };

    void destroy();
    void recordPartVelocities();
    /// Hits reported by Box2D in its last step. Called after every Box2D
    /// step of a simulation step: it also adds the contact impulse of that
    /// Box2D step to the hits between dynamic parts found so far.
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
    /// Where \p Entry is at \p Fraction of its motion in the last step (1 is
    /// now). With a \p Carrier only its motion relative to the carrier is
    /// cut short: the carrier stays where it is now, so 0 is where the entry
    /// would be had it moved rigidly with the carrier.
    Placement getMotionPlacement(const PartBody& Entry, const PartBody* Carrier, float Fraction) const;
    /// Where \p Entry was at \p Fraction of the last step (1 is now).
    Placement getPlacementDuringStep(const PartBody& Entry, float Fraction) const;
    /// Mass of a part for the impulse of a hit: its strike mass if it is
    /// kinematic now, kg.
    float getStrikeMass(const PartBody& Entry) const;

    uint32_t Id = 0;   ///< b2WorldId packed with b2StoreWorldId; 0 is null.
    int StepPasses = 1;
    int SubSteps = 4;
    std::optional<float> FighterFriction;   ///< Config::FighterFriction.
    std::vector<PartBody> PartBodies;
    std::vector<HitEvent> Hits;
    std::vector<SolvedHit> SolvedHits;   ///< Of the current step.
    float HitSpeedThreshold = 1.0f;
    std::vector<SlotPair> TouchingPosed;   ///< Sorted.
};

} // namespace fighter::physics
