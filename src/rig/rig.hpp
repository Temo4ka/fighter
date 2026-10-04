//===- rig/rig.hpp - Physical body of a fighter -----------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares rig::Rig, the hybrid body of a fighter
/// (docs/DEVELOPMENT_PLAN.md, phase 1.5): one rigid body per body part and
/// revolute joints between them, split in two by the rig file:
///  - kinematic parts (the pelvis and the legs) are moved by code. The
///    pelvis follows the PelvisController (walking, knockback) at the height
///    where the feet touch the floor; the legs are posed from the clips by
///    forward kinematics. They are solid: they push the opponent's physical
///    parts, but nothing pushes them.
///  - physical parts (torso, head, arms) are dynamic bodies driven by
///    PD-style joint motors towards the clip angles relative to their
///    parent, so the torso is held relative to the pelvis. Strikes and hit
///    reactions are physics.
///
/// There are no balance assists: a standing fighter cannot fall, because its
/// support is not physical. Stiffness scales the motors: it is raised during
/// own attacks, drops when the fighter is hit and recovers over time.
///
/// The pelvis height follows the leg pose: the body is lifted so that the
/// lowest posed part touches the floor, so a clip that bends the knees
/// (a crouch) lowers the pelvis with the feet staying on the floor. A planted
/// foot holds its place on the floor while the pelvis moves: the leg bends
/// to reach it (two-bone IK), so the feet do not slide when the walk cycle
/// and the walking speed disagree.
///
/// A strong hit knocks the fighter down: every part becomes dynamic and the
/// body falls as a ragdoll, pushed and spun by the hit; after a while the
/// kinematic parts take over again and bring the pelvis back into the
/// stance while the motors ramp up. A fighter told to stay down
/// (setStayDown, a knockout) does not get up.
///
/// Between two fighters (task 2.1): parts of the "passThrough" list pass the
/// same parts of the opponent (empty in the shipped rig: the arms collide,
/// a jab hits the raised guard), a limb stuck in the opponent lets go until
/// it is free ("unjam"), posed legs hit posed legs (physics::World), the
/// pelvises keep apart and away from the walls (rig/spacing.hpp). A posed
/// striking limb stops where it meets the opponent's posed parts
/// (stopAtContact): Box2D does not collide two kinematic bodies, so a kick
/// would go through the legs it hits.
///
/// The order of work per simulation step is explicit: set the targets, call
/// planMotion(), let the battle correct the plan (rig::keepApart or
/// getController()), call applyControl(), step the physics world, then
/// report hits with applyHit() (and rig::pushApartOnHit()).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/body.hpp"
#include "core/vec2.hpp"
#include "physics/body.hpp"
#include "physics/joint.hpp"
#include "physics/world.hpp"
#include "rig/pelvis_controller.hpp"
#include "rig/rig_def.hpp"

namespace fighter::rig {

/// Where a rig is created and how strong it is. Masses, motor values and the
/// walking speed scale come from the fighter's physical profile.
struct RigSetup {
    Vec2 Origin;                  ///< Floor point under the body, m.
    bool FacingRight = true;
    uint8_t FighterIndex = 0;     ///< PartRef::Fighter of the body parts.
    PerBodyPart<float> MassKg{};
    float MotorMaxTorque = 150.0f; ///< N*m for a joint of strength 1 at stiffness 1.
    float MotorGain = 12.0f;       ///< 1/s: motor speed per radian of angle error.
    float MoveSpeedScale = 1.0f;   ///< Multiplies ControlParams::WalkSpeed (DEX).
    /// How far the weapon of the loadout sticks out beyond the fist
    /// (stats::WeaponProps::ReachM), m; 0 is unarmed. The rig file says
    /// which part holds it (RigDef::Weapon).
    float WeaponReachM = 0.0f;
};

/// What the body is doing as a whole.
enum class Posture : uint8_t {
    Standing,     ///< Pelvis and legs kinematic, the rest physical.
    KnockedDown,  ///< Every part physical: a ragdoll on the floor.
    GettingUp,    ///< Pelvis and legs return to the stance, motors ramp up.
};

std::string_view getPostureName(Posture State);

/// The horizontal extent of a body, m.
struct ExtentX {
    float Min = 0.0f;
    float Max = 0.0f;
};

/// The physics world owns the bodies and joints: a rig must not outlive it.
class Rig {
public:
    /// Creates the bodies and joints in \p PhysWorld, in the reference pose.
    Rig(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup);

    /// \name Control (set before planMotion and applyControl)
    /// @{
    /// Target joint angles for a fighter facing right (see anim::Pose); the
    /// rig mirrors them when it faces left. The Pelvis entry is the lean.
    void setTargetAngles(const PerBodyPart<float>& Angles);
    /// Requested walking speed in the world, m/s; 0 stops.
    void setMoveVelocity(float Velocity);
    /// Stiffness without hits: 1 normally, higher during an attack.
    void setBaseStiffness(float Stiffness);
    /// Takes the planted feet where they stand now as the stance: standing
    /// still, a foot steps back under the body only when it is pushed
    /// FootRestepDistance further from there (not from the clip). Combat
    /// calls it when a walk has stopped on both feet, so that the feet left
    /// off the clip by the walk do not take an extra step. Lifting a foot
    /// forgets it.
    void keepFeetPlanted();
    /// For this step the feet follow the clip: a planted foot slides along
    /// the floor instead of holding its place. Combat calls it while a walk
    /// plays on quickly to a stop, so that the stop ends in the clip's pose.
    void slideFeet() { releaseFeet(); }
    /// Places every part in the target pose at rest, standing on the floor.
    /// For the start of a fight; it teleports the bodies.
    void snapToTargets();
    /// Turns the fighter to face right or left. Combat calls it when the
    /// opponent has got behind the fighter (2.3 decides when; turning while
    /// attacking looks odd). The body is mirrored about the pelvis in the
    /// next applyControl(), velocities included, so nothing jumps apart;
    /// while the fighter is down or getting up the turn waits until it
    /// stands. Target angles stay "as for facing right".
    void setFacing(bool FacingRight);
    /// Keeps a knocked-down fighter on the floor as a limp ragdoll: it does
    /// not get up until this is cleared. Combat sets it on a knockout; a
    /// fighter that is still standing (or getting up) collapses where it is.
    void setStayDown(bool Stay);
    /// @}

    /// Plans the pelvis motion of this step. The battle may correct the plan
    /// through getController() before applyControl().
    void planMotion(float Dt);
    PelvisController& getController() { return Controller; }
    const PelvisController& getController() const { return Controller; }

    /// Moves the kinematic parts, drives the motors of the physical ones and
    /// advances the posture (knocked down -> getting up -> standing) and the
    /// stiffness recovery, for the next physics step.
    void applyControl(float Dt);

    /// The fighter was hit with \p Impulse (N*s); \p Direction is +1 if the
    /// hit pushes to the right, -1 to the left. Same as the overload below
    /// with a horizontal push at the center of mass.
    void applyHit(float Impulse, float Direction);
    /// The fighter was hit with \p Impulse (N*s) at \p Point, pushed along
    /// the unit vector \p Direction. Stiffness drops; a standing fighter gets
    /// knockback (the horizontal part of the push) or, if the hit is strong
    /// enough, is knocked down: it falls the way it was pushed and spins
    /// about its center of mass by where the hit landed (a head hit topples
    /// it backwards, a low kick sweeps the legs). Combat calls it for every
    /// landed strike with HitEvent::Point.
    void applyHit(float Impulse, Vec2 Direction, Vec2 Point);
    /// The same as above, but the caller decides whether the hit knocks the
    /// fighter down (combat's reaction levels, task 2.3; the rig's
    /// knockdownSpeed is then not used): with \p KnockDown a fighter that is
    /// not lying already falls the way it was pushed, spun by where the hit
    /// landed; otherwise it is only pushed back. Prefer this one with
    /// HitEvent::Point over the overload without a point.
    void applyHit(float Impulse, Vec2 Direction, Vec2 Point, bool KnockDown);
    /// The caller decides about the knockdown, the push is horizontal at the
    /// center of mass (no spin). Kept for combat (task 2.3).
    void applyHit(float Impulse, float Direction, bool KnockDown);
    /// Adds knockback that moves the pelvis by about \p Distance (m, signed
    /// along X) in total: the push-out of rig::pushApartOnHit().
    void addPush(float Distance);
    /// Records which arena wall the fighter touches; rig::keepApart() calls
    /// it every step. The pelvis stops at [MinX, MaxX]; a ragdoll touches
    /// the wall faces at +-WallX.
    void updateWallContact(float MinX, float MaxX, float WallX);
    /// Call after the physics step. Do the posed parts of \p Strikers touch
    /// a posed part of the opponent? If one sank deeper than \p MaxDepth (m)
    /// during the step, every posed part goes back along its motion of the
    /// step to where the strikers were MaxDepth deep (the leg stays at the
    /// contact). Only a new contact counts, unless \p Holding (held at a
    /// contact already); see physics::World::findPosedStop. Returns the
    /// share of the step's motion kept (1 for a contact that needed no
    /// stop), or nullopt if there is no contact. Parts that are not posed now
    /// are ignored.
    std::optional<float> stopAtContact(const std::bitset<BodyPartCount>& Strikers, float MaxDepth, bool Holding);

    /// \name State
    /// @{
    const ControlParams& getControl() const { return Control; }
    /// PartRef::Fighter of the body parts.
    uint8_t getFighterIndex() const { return FighterIndex; }
    bool isFacingRight() const { return Facing > 0.0f; }
    /// Has setFacing() asked for a turn that has not happened yet?
    bool isTurnPending() const { return RequestedFacing != Facing; }
    Posture getPosture() const { return CurrentPosture; }
    /// Time spent in the current posture, s.
    float getPostureSec() const { return PostureSec; }
    bool isStayingDown() const { return StayDown; }
    /// Is \p Part moved by code right now (not physical)?
    bool isKinematic(BodyPart Part) const;
    /// Has \p Part let go of the opponent: it passes through the opponent
    /// until it is free (ControlParams::JamAngle)?
    bool isUnjamming(BodyPart Part) const;
    float getStiffness() const;
    /// Mass of the whole fighter (the profile's), kg.
    float getTotalMass() const { return TotalMass; }
    /// Walking speed forwards with the profile's scale, m/s.
    float getWalkSpeed() const { return Control.WalkSpeed * MoveSpeedScale; }
    /// Motor torque of a joint of strength 1 at stiffness 1, N*m.
    float getMotorMaxTorque() const { return MotorMaxTorque; }
    /// Motor speed per radian of angle error at stiffness 1, 1/s.
    float getMotorGain() const { return MotorGain; }
    /// Sum of the absolute torques of all joint motors in the last step, N*m.
    float getMotorTorqueSum() const;
    Vec2 getCenterOfMass() const;
    /// Point on the floor between the feet (lifted when both feet are in the air).
    Vec2 getFloorPoint() const;
    Vec2 getPartPosition(BodyPart Part) const;
    float getPartAngle(BodyPart Part) const;
    /// Angle of the joint whose child is \p Part, unmirrored (as in a pose), rad.
    float getJointAngle(BodyPart Part) const;
    void getPartTransforms(std::vector<PartTransform>& Out) const;
    /// Where the body is along the arena, from the shapes of all parts, m.
    ExtentX getExtentX() const;
    /// The arena wall the fighter touches: -1 left, +1 right, 0 none
    /// (updateWallContact()).
    int getWallSide() const { return WallSide; }
    /// Is the fighter's back against a wall: it touches the wall behind it
    /// and cannot retreat? Combat fills FighterView::AgainstWall from it.
    bool isAgainstWall() const { return WallSide != 0 && static_cast<float>(WallSide) == -Facing; }
    /// Is this foot planted and held in place on the floor?
    bool isFootLocked(BodyPart Foot) const;
    /// How high the sole of \p Foot would be above the floor if the body
    /// stood in the pose \p Angles (as for setTargetAngles(), clamped to the
    /// joint limits) with its lowest posed part on the floor, m. 0 for a foot
    /// that carries the body. Combat finds the phases of a walk cycle where
    /// both feet stand with it; it is the same lift as standing.
    float getSoleHeight(const PerBodyPart<float>& Angles, BodyPart Foot) const;
    /// How far the weapon sticks out beyond the fist, m; 0 if unarmed.
    float getWeaponReach() const { return WeaponReach; }
    /// How deep posed \p Part overlaps the opponent's posed parts, m; 0 if
    /// it does not touch them or is not posed now.
    float getPosedPenetration(BodyPart Part) const;
    /// Did stopAtContact() find a contact (and stop there) in the last step?
    bool isStoppedAtContact() const { return StoppedAtContact; }
    /// @}

    /// Hurtboxes come from the physics world's debug draw; the rig draws
    /// joint limits, the target pose ghost, motors, velocities (with the
    /// pelvis controller), the center of mass, planted feet, wall contact,
    /// freed limbs, posed strikers stopped at a contact and the weapon, and
    /// fills the panel lines "P1 facing", "P1 wall", "P1 feet", "P1 limbs",
    /// "P1 posed overlap". Does nothing in the release build.
    void drawDebug() const;

private:
    struct PartState {
        PartDef Shape;            ///< Mirrored for the facing, relative to the body origin.
        physics::Body Handle;
        Vec2 Size;                ///< Bounds of the shape in the body frame.
        float Mass = 0.0f;        ///< kg, also while the body is kinematic.
        bool Kinematic = false;   ///< Moved by code while the fighter is not knocked down.
        uint64_t CollisionMask = 0; ///< While standing; a knockdown also drops the posed parts.
        bool Unjam = false;       ///< May let go of the opponent (RigDef::Unjam).
        BodyPart Limb = BodyPart::Torso; ///< Topmost part of its chain of unjam parts.
        float StuckSec = 0.0f;    ///< Limb only: how long it has been stuck in the opponent.
        bool Freed = false;       ///< Passes through the opponent until it is free.
    };

    struct JointState {
        BodyPart Child = BodyPart::Torso;
        BodyPart Parent = BodyPart::Pelvis;
        physics::RevoluteJoint Handle;
        float Strength = 1.0f;
        float LowerAngle = 0.0f;  ///< Mirrored, rad.
        float UpperAngle = 0.0f;
        float Target = 0.0f;      ///< Mirrored and clamped to the limits, rad.
        Vec2 AnchorInParent;      ///< Hinge in the parent's body frame (reference pose).
        Vec2 ChildFromAnchor;     ///< Child body origin relative to the hinge (reference pose).
        float RestDirection = 0.0f; ///< Direction of the child from the hinge in the reference pose, rad.
    };

    /// A posed leg: hip, knee and ankle joints down to a foot (indices into
    /// Joints), and how its foot is held on the floor.
    struct Leg {
        size_t Hip = 0;
        size_t Knee = 0;
        size_t Ankle = 0;
        BodyPart Foot = BodyPart::FootL;
        float Length = 0.0f;      ///< Hip to ankle with a straight knee, m.
        bool Locked = false;      ///< Planted: the ankle holds LockX.
        bool Stepping = false;    ///< Steps back under the body; plants when there.
        float LockX = 0.0f;       ///< World X of the planted ankle, m.
        float OffsetX = 0.0f;     ///< Ankle X minus where the clip puts it, m.
        float KeptOffsetX = 0.0f; ///< The offset keepFeetPlanted() took as the stance, m.
    };

    /// Where a body origin is and how the body is turned.
    struct Placement {
        Vec2 Position;
        float Angle = 0.0f;
    };

    /// The weapon shape on its part, in the part's body frame.
    struct WeaponShape {
        BodyPart Part = BodyPart::ForearmR;
        Vec2 Grip;
        Vec2 Tip;
        float Radius = 0.0f;
    };

    const PartState& getPart(BodyPart Part) const { return Parts[static_cast<size_t>(Part)]; }
    PartState& getPart(BodyPart Part) { return Parts[static_cast<size_t>(Part)]; }
    void createParts(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup,
                     PerBodyPart<Vec2>& Centers);
    void createJoints(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup,
                      const PerBodyPart<Vec2>& Centers);
    void setStrikeMasses(physics::World& PhysWorld);
    const JointState* findJoint(BodyPart Child) const;
    void findLimbs(const RigDef& Def);
    void findLegs();
    /// The target pose by forward kinematics from a root placement, with
    /// \p Corrections added to the joint targets (indexed by child part).
    PerBodyPart<Placement> computeTargetPose(Placement RootPlacement,
                                             const PerBodyPart<float>& Corrections = {}) const;
    /// Root placement at the controller position, at the height where the
    /// lowest kinematic part touches the floor.
    Placement getStandingRoot() const;
    float getPostureStiffness() const;
    void advancePosture();
    void moveKinematicParts(float Dt);
    /// Holds planted feet in place: returns the joint corrections of the
    /// legs for the uncorrected pose \p Pose and updates the locks.
    PerBodyPart<float> plantFeet(const PerBodyPart<Placement>& Pose, float Dt);
    /// Joint corrections that bend \p Limb so that its ankle reaches
    /// \p Ankle with the foot turned as in \p Pose.
    void reachAnkle(const Leg& Limb, const PerBodyPart<Placement>& Pose, Vec2 Ankle,
                    PerBodyPart<float>& Corrections) const;
    void releaseFeet();
    /// How far a planted foot is from where it should stand still.
    static float getRestepDistance(const Leg& Limb);
    void driveMotors();
    void updateJams(float Dt);
    void setLimbFreed(BodyPart Limb, bool Freed);
    void refreshCollisionMask(PartState& Part) const;
    void knockDown(Vec2 Velocity, float Spin);
    void startGettingUp();
    void turnAround();
    void drawTargetPose() const;
    void drawJointsAndMotors() const;
    void drawController() const;
    void drawFeetAndLimbs() const;
    void drawWeapon() const;
    void fillPanel() const;

    physics::World* Physics = nullptr;   ///< Switches parts between kinematic and dynamic.
    ControlParams Control;
    BodyPart Root = BodyPart::Pelvis;
    float Facing = 1.0f;          ///< +1 facing right, -1 facing left.
    float RequestedFacing = 1.0f; ///< setFacing(); applied in applyControl().
    uint8_t FighterIndex = 0;
    float MotorMaxTorque = 0.0f;
    float MotorGain = 0.0f;
    float MoveSpeedScale = 1.0f;
    float TotalMass = 0.0f;

    PerBodyPart<PartState> Parts{};
    std::vector<JointState> Joints;   ///< Parents before children.
    std::vector<Leg> Legs;
    PerBodyPart<float> TargetAngles{};///< As given (unmirrored).

    PelvisController Controller;
    Posture CurrentPosture = Posture::Standing;
    float PostureSec = 0.0f;
    bool StayDown = false;
    /// Kinematic parts as they lay when getting up started.
    PerBodyPart<Placement> GetUpFrom{};

    float BaseStiffness = 1.0f;
    float HitFactor = 1.0f;           ///< 1 without hits, drops to MinStiffness.
    int WallSide = 0;
    float WeaponReach = 0.0f;
    WeaponShape Weapon;
    /// The strikers stopAtContact() last held back, and whether it did so
    /// in the last step; for the debug draw.
    std::bitset<BodyPartCount> StoppedParts;
    bool StoppedAtContact = false;
    /// The last knockdown push, for the debug draw: where and how hard.
    Vec2 KnockdownPoint;
    Vec2 KnockdownVelocity;
    float KnockdownSpinRate = 0.0f;
};

} // namespace fighter::rig
