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
///    reactions are physics. Standing, the pelvis carries them: every step
///    they take the change of the pelvis motion (not of a knockback), the
///    motors add the clip's own joint speed and always have the torque to
///    reach the pose without overshoot (the inertia of the child chain),
///    and their weight is taken off, so at rest and walking the upper body
///    follows the clip; hits, knockback and knockdowns still swing it.
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
/// Between two fighters (task 2.1) nothing passes through the opponent:
/// parts of the "passThrough" list pass the same parts of the opponent
/// (empty in the shipped rig: the arms collide, a jab hits the raised
/// guard), a limb stuck in the opponent yields: its motors soften and it
/// pulls back to the rig's yield pose, still colliding ("unjam"), posed legs
/// hit posed legs (physics::World), the bodies keep apart and away from the
/// walls (rig/spacing.hpp), a knocked-down body collides with the standing
/// fighter's legs. A posed striking limb stops where it meets the
/// opponent's posed parts (stopAtContact), and a posed leg swung too deep
/// into the opponent is held back (holdLimbsBack): Box2D does not collide
/// two kinematic bodies, so a kick would go through the legs it hits.
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
#include <span>
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

/// How far the physical parts are from the target pose (Rig::getPoseError).
struct PoseError {
    float Angle = 0.0f;               ///< Largest turn of a part away from the target pose, rad.
    BodyPart Part = BodyPart::Torso;  ///< The part that is furthest off.
};

/// Where a part of a rig is going to be (Rig::predictBody).
struct PartPlacement {
    physics::Body Handle;
    Vec2 Position;            ///< Body origin, m.
    float Angle = 0.0f;       ///< rad.
    /// A striker of an attack (Rig::setStrikingParts) is in two places: where
    /// it is now, carried with the pelvis (Position, Angle), and where the
    /// clip poses it (these). It overlaps only if it overlaps in both: its
    /// own motion into the opponent is stopped by Rig::stopAtContact, the
    /// opponent's into it is the spacing's to prevent.
    bool Striking = false;
    Vec2 PosedPosition;
    float PosedAngle = 0.0f;
};

/// Where a foot stands, as for a fighter facing right, relative to the
/// floor point under the pelvis: X forward, Y up from the floor.
struct FootPlacement {
    Vec2 Ankle;               ///< The ankle hinge, m.
    float Angle = 0.0f;       ///< The foot's angle in the world, rad.
    float SoleHeight = 0.0f;  ///< The lowest point of the foot above the floor, m.
    /// The rig holds it planted on the floor (measureLegsNow() only).
    bool Planted = false;
};

/// How a body stands on its legs (Rig::measureLegs, Rig::measureLegsNow).
struct LegStance {
    float PelvisHeight = 0.0f;  ///< The pelvis body origin above the floor, m.
    FootPlacement Left;
    FootPlacement Right;

    /// The placement of \p Foot: FootL or FootR (any other part: Right).
    const FootPlacement& getFoot(BodyPart Foot) const { return Foot == BodyPart::FootL ? Left : Right; }
    FootPlacement& getFoot(BodyPart Foot) { return Foot == BodyPart::FootL ? Left : Right; }
    /// How far apart the ankles are along the floor, m.
    float getSpread() const;
    /// The foot whose ankle is further forward: FootL or FootR.
    BodyPart getFrontFoot() const { return Right.Ankle.X > Left.Ankle.X ? BodyPart::FootR : BodyPart::FootL; }
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
    /// The angles of setTargetAngles() are those of the pose after the
    /// pelvis travels \p Travel (m, along X) in this step: a walk cycle
    /// advanced by the planned travel, or by the push of the last step;
    /// these are the angles without it. The joints then follow the travel
    /// the pelvis really makes after the spacing (getTravelShare()): in
    /// between, the angles are interpolated by its share, so a walk that the
    /// spacing holds back steps only as far as the pelvis goes (predictBody()
    /// poses it the same way). Call after setTargetAngles(), which drops the
    /// link.
    void setTravelPose(const PerBodyPart<float>& StillAngles, float Travel);
    /// Requested walking speed in the world, m/s; 0 stops.
    void setMoveVelocity(float Velocity);
    /// Stiffness without hits: 1 normally, higher during an attack.
    void setBaseStiffness(float Stiffness);
    /// Takes the planted feet where they stand now as the stance: standing
    /// still, a foot steps back under the body only when it is pushed
    /// FootRestepDistance further from there (not from the clip), and the
    /// clip may pull it further than ControlParams::FootLockSlip without
    /// dragging it (as far as the leg reaches). Combat calls it while a walk
    /// stops on both feet and while the legs step into an action, so that
    /// the feet left off the clip do not slide or take an extra step.
    /// Lifting a foot forgets it.
    void keepFeetPlanted();
    /// The lifted feet go exactly where the target pose puts them: their
    /// offset from the clip (a lifted foot returns to the clip from where it
    /// stood) is dropped. Combat calls it when it starts posing the feet
    /// itself (a step into an action or into the rest), from where they are.
    void dropLiftedFootOffsets();
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
    /// The posed parts that strike now (the strikers of an attack before it
    /// stopped at a contact): they stop at the opponent by themselves
    /// (stopAtContact), so the spacing of the fighters treats them apart
    /// (predictBody). \p Attacking: the strikers of the attack all through
    /// it; a lifted foot among them does not step down where it is, so the
    /// spacing keeps no floor below it clear. Combat sets both every step;
    /// none by default.
    void setStrikingParts(const std::bitset<BodyPartCount>& Striking, const std::bitset<BodyPartCount>& Attacking) {
        StrikingParts = Striking;
        AttackingParts = Attacking;
    }
    /// Keeps a knocked-down fighter on the floor as a limp ragdoll: it does
    /// not get up until this is cleared. Combat sets it on a knockout; a
    /// fighter that is still standing (or getting up) collapses where it is.
    void setStayDown(bool Stay);
    /// @}

    /// Plans the pelvis motion of this step. The battle may correct the plan
    /// through getController() before applyControl().
    void planMotion(float Dt);
    PelvisController& getController() { return Controller; }
    /// Pushes the whole body by \p Delta (m, along X) in this step: the
    /// planned pelvis position and the planted feet move together, so a
    /// foot pressed into the opponent's leaves with the body. A push that
    /// only takes back some of the step's own planned travel (a walk slowed
    /// down) leaves the planted feet where they are: the fighter just
    /// travels less. The spacing of the fighters calls it (rig/spacing.hpp)
    /// between planMotion() and applyControl().
    void pushBody(float Delta);
    /// The share of the travel of setTravelPose() the pelvis makes after the
    /// corrections so far (PelvisController::getTravelShare); the posed
    /// joints follow it. 1 without a link.
    float getTravelShare() const { return Controller.getTravelShare(Controller.getPlannedX(), PoseTravel); }
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
    /// Call after the physics step. Did the posed parts of \p Strikers run
    /// into the opponent (its posed parts, or a part the solver could not
    /// push away) by their own motion, relative to the pelvis? If one sank
    /// deeper than \p MaxDepth (m) during the step, the posed limbs of the
    /// strikers go back along that relative motion to where the strikers
    /// were MaxDepth deep (the leg stays at the contact and on its hip); the
    /// pelvis and the other limbs keep the step's motion. See
    /// physics::World::findPosedStop. Returns the share of the step's motion
    /// kept (1 for a contact that needed no stop), or nullopt if there is no
    /// contact. Parts that are not posed now are ignored.
    std::optional<float> stopAtContact(const std::bitset<BodyPartCount>& Strikers, float MaxDepth);

    /// Call after the physics step and stopAtContact(). A posed limb (a leg)
    /// whose own motion in the step, relative to the pelvis, took it deeper
    /// than \p MaxDepth (m) into the opponent goes back along that motion to
    /// MaxDepth deep (it stays on its hip; the pelvis keeps its motion).
    /// Nothing in physics stops a posed limb, and the spacing of the
    /// fighters (rig/spacing.hpp) can push the bodies apart only so fast: a
    /// foot swung through the opponent's in one step, or the thigh of a kick
    /// rising into a guard the solver cannot push away, stops there instead.
    /// Touching is fine; the strikers stopAtContact() already stopped are no
    /// deeper than that.
    void holdLimbsBack(float MaxDepth);

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
    /// Does the limb of \p Part yield: it was stuck in the opponent, so its
    /// motors are soft and it pulls back to the yield pose
    /// (ControlParams::JamAngle, YieldSec)? It still collides.
    bool isYielding(BodyPart Part) const;
    float getStiffness() const;
    /// Mass of the whole fighter (the profile's), kg.
    float getTotalMass() const { return TotalMass; }
    /// Walking speed forwards with the profile's scale, m/s.
    float getWalkSpeed() const { return Control.WalkSpeed * MoveSpeedScale; }
    /// The pelvis travel the walk clip's legs are posed for per second, m/s:
    /// a faster fighter steps more often, not longer.
    float getStrideSpeed() const { return Control.WalkSpeed; }
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
    /// How the body would stand in the pose \p Angles (as for
    /// setTargetAngles(), clamped to the joint limits) with its lowest posed
    /// part on the floor: the pelvis height and where the feet are. Nothing
    /// moves.
    LegStance measureLegs(const PerBodyPart<float>& Angles) const;
    /// How the body stands now: the pelvis height and the feet where their
    /// bodies are (planted feet held off the clip included).
    LegStance measureLegsNow() const;
    /// Bends the leg of \p Foot in \p Angles (as for setTargetAngles()) so
    /// that, with the pelvis \p PelvisHeight above the floor, its ankle is
    /// at \p Ankle and the foot at \p FootAngle in the world (both as in
    /// FootPlacement). Two-bone IK: the knee bends the way a knee bends (into
    /// its joint range); a point out of reach gets the nearest the leg can do. The
    /// other joints are left as they are. Nothing moves.
    void reachFoot(PerBodyPart<float>& Angles, BodyPart Foot, float PelvisHeight, Vec2 Ankle, float FootAngle) const;
    /// How far the weapon sticks out beyond the fist, m; 0 if unarmed.
    float getWeaponReach() const { return WeaponReach; }
    /// How deep posed \p Part overlaps the opponent's posed parts, m; 0 if
    /// it does not touch them or is not posed now.
    float getPosedPenetration(BodyPart Part) const;
    /// Where the body will be after the next applyControl() if the pelvis
    /// controller ends the step at \p RootX: the posed parts exactly (the
    /// pose of the target angles with the planted feet held, or the blend of
    /// getting up), the torso and the head (physical, but held on the
    /// pelvis) moved along with the pelvis. A pelvis away from the planned
    /// one takes the planted feet along (pushBody()). The parts of
    /// setStrikingParts() stop at the opponent by themselves: they are
    /// both where they are now, moved along with the pelvis, and where the
    /// clip poses them (PartPlacement::Striking), so the opponent does not
    /// walk into them. The arms are left out (the solver keeps them off
    /// the opponent, and they yield); none while the fighter is knocked down. The
    /// spacing tries pelvis positions with it before it corrects the plan
    /// (rig/spacing.hpp). Nothing moves.
    std::vector<PartPlacement> predictBody(float RootX, float Dt) const;
    /// The smallest gap between the shapes of \p Own and \p Other, m;
    /// negative: how deep they overlap (a striker: the larger of its gaps
    /// at its two places, PartPlacement::Striking). Nothing moves.
    float measureGap(std::span<const PartPlacement> Own, std::span<const PartPlacement> Other) const;
    /// Are posed parts striking now (setStrikingParts())?
    bool isStriking() const { return StrikingParts.any(); }
    /// Did stopAtContact() find a contact (and stop there) in the last step?
    bool isStoppedAtContact() const { return StoppedAtContact; }
    /// How far the physical parts (torso, head, arms) are from the target
    /// pose posed from where the pelvis is now (the ghost of the debug
    /// draw): the largest difference of a part's angle in the world.
    PoseError getPoseError() const;
    /// Did the last applyControl() carry the physical parts along with the
    /// pelvis (ControlParams::CarrierTransfer)? Only a standing fighter is
    /// carried: knocked down or getting up, the body is a ragdoll.
    bool isCarrying() const { return Carrying; }
    /// @}

    /// Hurtboxes come from the physics world's debug draw; the rig draws
    /// joint limits, the target pose ghost, motors, velocities (with the
    /// pelvis controller), the center of mass, planted feet, wall contact,
    /// freed limbs, posed strikers stopped at a contact and the weapon, the
    /// physical parts away from the ghost (a line to it with the angle), and
    /// fills the panel lines "P1 facing", "P1 wall", "P1 feet", "P1 limbs",
    /// "P1 pose" (pose error, carrier transfer, holding torque),
    /// "P1 posed overlap". Does nothing in the release build.
    void drawDebug() const;

private:
    struct PartState {
        PartDef Shape;            ///< Mirrored for the facing, relative to the body origin.
        physics::Body Handle;
        Vec2 Size;                ///< Bounds of the shape in the body frame.
        float Mass = 0.0f;        ///< kg, also while the body is kinematic.
        float Inertia = 0.0f;     ///< About the center of mass, kg*m^2, also while kinematic.
        bool Kinematic = false;   ///< Moved by code while the fighter is not knocked down.
        bool Unjam = false;       ///< May yield when stuck in the opponent (RigDef::Unjam).
        BodyPart Limb = BodyPart::Torso; ///< Topmost part of its chain of unjam parts.
        float StuckSec = 0.0f;    ///< Limb only: how long it has been stuck in the opponent.
        bool Yielding = false;    ///< Soft, pulling back to the yield pose.
        float YieldSec = 0.0f;    ///< Limb only: how long it has been yielding.
    };

    struct JointState {
        BodyPart Child = BodyPart::Torso;
        BodyPart Parent = BodyPart::Pelvis;
        physics::RevoluteJoint Handle;
        float Strength = 1.0f;
        float LowerAngle = 0.0f;  ///< Mirrored, rad.
        float UpperAngle = 0.0f;
        float Wish = 0.0f;        ///< The clip's angle, mirrored and clamped to the limits, rad.
        float StillWish = 0.0f;   ///< Wish without the step's travel (setTravelPose()), rad.
        float Target = 0.0f;      ///< What the motor drives to: Wish, or the yield pose, rad.
        float YieldWish = 0.0f;   ///< Wish when the limb started to yield, rad.
        float PreviousTarget = 0.0f; ///< Target in the last step: the clip's joint speed, rad.
        float HoldTorque = 0.0f;  ///< Torque limit for the inertia and the weight of the last step, N*m.
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
        /// keepFeetPlanted() took it: it holds its place as far as the leg
        /// reaches, not only within FootLockSlip, until it is lifted.
        bool Kept = false;
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
    /// Root placement at \p RootX, at the height where the lowest
    /// kinematic part touches the floor.
    Placement getStandingRoot(float RootX, const PerBodyPart<float>& Corrections = {}) const;
    float getPostureStiffness() const;
    void advancePosture();
    void moveKinematicParts(float Dt);
    /// The pose of the kinematic parts with the root at \p RootX: the target
    /// pose with \p Corrections, the planted feet of \p Limbs held (their
    /// locks are updated), or the blend of getting up at \p PostureTime.
    PerBodyPart<Placement> computePosedPose(float RootX, std::vector<Leg>& Limbs, float Dt, float PostureTime,
                                            const PerBodyPart<float>& Corrections) const;
    /// Holds planted feet in place: returns the joint corrections for the
    /// pose \p Pose (posed with the corrections \p Base, which it keeps for
    /// the joints it does not bend) and updates the locks of \p Limbs.
    /// \p RootDrop: how far the root went down below the pose (getReachDrop()).
    PerBodyPart<float> plantFeet(const PerBodyPart<Placement>& Pose, std::vector<Leg>& Limbs, float Dt,
                                 const PerBodyPart<float>& Base, float RootDrop) const;
    /// How far the root of \p Pose must go down so that the legs reach their
    /// kept planted feet (keepFeetPlanted()), m; 0 if they do.
    float getReachDrop(const PerBodyPart<Placement>& Pose, const std::vector<Leg>& Limbs) const;
    /// How far the planted feet go along when the pelvis ends the step at
    /// \p RootX, relative to its own plan: only the part beyond the step's
    /// planned travel (pushBody()), m.
    float getFootDrag(float RootX) const;
    /// Joint corrections (to the targets) of the pose at \p Share of the
    /// planned travel (setTravelPose()).
    PerBodyPart<float> getTravelCorrections(float Share) const;
    /// Sets the joints to the share of the planned travel the pelvis makes
    /// (setTravelPose()); applyControl() calls it before the commit.
    void followTravel();
    /// Joint corrections that bend \p Limb so that its ankle reaches
    /// \p Ankle with the foot turned as in \p Pose.
    /// The knee takes the solution within its limits, else the one closer
    /// to \p KneeHint (rad, mirrored), and bends no deeper than \p MaxBend
    /// (rad, along the way it bends), if given.
    void reachAnkle(const Leg& Limb, const PerBodyPart<Placement>& Pose, Vec2 Ankle,
                    PerBodyPart<float>& Corrections, float KneeHint, std::optional<float> MaxBend = {}) const;
    /// The joint targets of the pose \p Angles (as for setTargetAngles(),
    /// clamped to the limits) as corrections to the current targets.
    PerBodyPart<float> getAngleCorrections(const PerBodyPart<float>& Angles) const;
    /// Where the ankle hinge of \p Limb is in \p Pose.
    Vec2 getAnkleInPose(const Leg& Limb, const PerBodyPart<Placement>& Pose) const;
    void releaseFeet();
    /// The topmost part of the limb of \p Part below the root (a thigh for
    /// a foot); the part itself if its parent is the root.
    BodyPart getLimbTop(BodyPart Part) const;
    /// Puts the posed parts of the limb whose topmost part is \p Top back
    /// along their motion of the last step relative to the pelvis
    /// (physics::World::rewindBody).
    void rewindLimb(BodyPart Top, float Fraction);
    /// How far a planted foot is from where it should stand still.
    static float getRestepDistance(const Leg& Limb);
    /// Gives the physical parts the change of the pelvis motion of this
    /// step (\p OldVelocity, \p OldSpin: the pelvis motion before it).
    void carryPhysicalParts(Vec2 OldVelocity, float OldSpin);
    /// Gravity scale of the physical parts in the current posture.
    float getCarriedGravityScale() const;
    void driveMotors(float Dt);
    /// The torque \p Joint needs to move its child chain without overshoot
    /// at motor gain \p Gain and to hold its weight, N*m.
    float getHoldTorque(const JointState& Joint, float Gain) const;
    void updateJams(float Dt);
    void setLimbYielding(BodyPart Limb, bool Yielding);
    /// Would the limb whose topmost part is \p Limb, posed at the clip's
    /// angles from where its parent is now, overlap the opponent? The
    /// opponent's limbs of the "unjam" list (its arms) do not block it: a
    /// guard comes back against the opponent's guard and rests on it (the
    /// solver keeps them apart), as two guards do that never jammed.
    bool isWishBlocked(BodyPart Limb) const;
    /// Joint targets from the wishes and the yielding limbs.
    void refreshTargets();
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
    PerBodyPart<float> YieldAngles{}; ///< RigDef::YieldAngles (unmirrored).
    std::bitset<BodyPartCount> YieldPosed;
    std::bitset<BodyPartCount> UnjamParts;   ///< RigDef::Unjam.

    PelvisController Controller;
    float PoseTravel = 0.0f;          ///< setTravelPose(): the travel the target angles assume, m.
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
    std::bitset<BodyPartCount> StrikingParts;   ///< setStrikingParts().
    std::bitset<BodyPartCount> AttackingParts;  ///< setStrikingParts().
    /// The strikers stopAtContact() last held back, and whether it did so
    /// in the last step; for the debug draw.
    std::bitset<BodyPartCount> StoppedParts;
    bool StoppedAtContact = false;
    /// The limbs (topmost parts) holdLimbsBack() held back in the last step.
    std::bitset<BodyPartCount> HeldLimbs;
    /// The last knockdown push, for the debug draw: where and how hard.
    Vec2 KnockdownPoint;
    Vec2 KnockdownVelocity;
    float KnockdownSpinRate = 0.0f;
    /// Carrier transfer: on in the last applyControl(), and the knockback of
    /// the pelvis plan (planMotion()) and of the step before.
    bool Carrying = false;
    float PlannedKnockback = 0.0f;
    float CarriedKnockback = 0.0f;
};

} // namespace fighter::rig
