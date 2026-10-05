//===- rig/rig_def.hpp - Rig description loaded from JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares RigDef, the description of a fighter's body that is
/// loaded from data/rigs/<id>.json: body parts and their shapes, joints with
/// angle limits, which parts are kinematic, and the parameters of the body
/// controller (pelvis controller, motors, hit reaction, knockdown).
///
/// Geometry is given in the reference pose: standing straight, arms hanging
/// down, facing right, in meters relative to the fighter origin (the floor
/// point under the body). Joint angles are measured from this pose, so zero
/// for every joint is the reference pose. Angles in the file are in degrees.
///
/// Masses and motor torque are not here: they come from the fighter's
/// physical profile (stats).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <filesystem>
#include <string_view>
#include <vector>

#include "core/body.hpp"
#include "core/vec2.hpp"
#include "physics/body.hpp"

namespace fighter::rig {

struct PartDef {
    BodyPart Part = BodyPart::Torso;
    physics::ShapeKind Shape = physics::ShapeKind::Capsule;
    Vec2 Begin;              ///< Capsule: center of the first cap.
    Vec2 End;                ///< Capsule: center of the second cap.
    Vec2 Center;             ///< Circle and box: center.
    Vec2 HalfExtents;        ///< Box: half width and half height.
    float Radius = 0.0f;     ///< Circle and capsule radius, box rounding.
    float Friction = 0.6f;
};

/// A hinge between a part and its parent. A joint is named by its child part.
struct JointDef {
    BodyPart Child = BodyPart::Torso;
    BodyPart Parent = BodyPart::Pelvis;
    Vec2 Anchor;                 ///< Hinge position in the reference pose.
    float LowerAngle = 0.0f;     ///< rad.
    float UpperAngle = 0.0f;     ///< rad.
    float Strength = 1.0f;       ///< Share of the profile's motor torque this joint gets.
};

/// Parameters of the body controller (the "control" object of a rig file;
/// docs/TUNING.md describes each one).
struct ControlParams {
    /// \name Motors of the physical parts
    /// @{
    float TorqueScale = 1.0f;          ///< Multiplies the profile's motor torque.
    float GainScale = 1.0f;            ///< Multiplies the profile's motor gain.
    float MaxJointSpeed = 15.0f;       ///< Motor speed limit, rad/s.
    float AngularDamping = 0.5f;       ///< Of every body part, 1/s.
    /// @}

    /// \name Smooth body: the physical parts follow the clip while standing
    /// @{
    /// Share of the pelvis's change of motion in a step that the physical
    /// parts take over at once (carrier transfer): walking, stopping, turning
    /// and the lean do not swing the upper body. 0 leaves it to the joints.
    float CarrierTransfer = 1.0f;
    /// The same for the knockback part of the pelvis motion: 0 lets the
    /// upper body lag behind a knockback (it shows the hit), 1 carries it.
    float KnockbackTransfer = 0.0f;
    /// Share of the clip's own joint speed the motors add to the error
    /// correction (feed-forward): 1 follows a moving pose without lag.
    float FeedForward = 1.0f;
    /// Share of gravity taken off the physical parts while standing: the
    /// motors need not hold their weight. Knocked down they fall fully.
    float GravityCompensation = 1.0f;
    /// The motors hold this many times the weight of a limb held out
    /// sideways (the gravity left by GravityCompensation), ×.
    float HoldGravityMargin = 1.5f;
    /// A motor brings a limb back from this error without overshoot,
    /// whatever its mass: its torque covers the limb's inertia times
    /// (gain × stiffness)^2 × this angle, rad.
    float DampedErrorAngle = 0.3f;
    /// @}

    /// \name Pelvis controller
    /// @{
    float WalkSpeed = 1.2f;            ///< m/s; the profile's MoveSpeedScale (DEX) multiplies it.
    float BackwardSpeedScale = 0.7f;   ///< Walking backwards is slower.
    float WalkAcceleration = 8.0f;     ///< How fast the walking speed is gained, m/s^2.
    /// How fast it is lost when the fighter slows down or stops, m/s^2: the
    /// pelvis should stop about as fast as the walk cycle stops on both feet.
    float WalkDeceleration = 20.0f;
    /// @}

    /// \name Hit reaction
    /// @{
    float MinStiffness = 0.25f;        ///< Lowest stiffness after a hit.
    float StiffnessPerImpulse = 0.03f; ///< Stiffness lost per N*s of hit impulse.
    float StiffnessRecovery = 0.8f;    ///< Stiffness regained per second.
    /// Multiplies the knockback speed, impulse / mass of the whole fighter.
    float KnockbackScale = 1.0f;
    float KnockbackDecay = 4.0f;       ///< Exponential decay of the knockback speed, 1/s.
    /// @}

    /// \name Knockdown
    /// @{
    /// A hit whose knockback speed (impulse / mass) reaches this knocks the
    /// fighter down: the threshold impulse grows with the fighter's mass, m/s.
    float KnockdownSpeed = 0.7f;
    float KnockdownSec = 1.5f;         ///< Time on the floor, s.
    float GetUpSec = 0.8f;             ///< Time to get back into the stance, s.
    /// Motor stiffness while down; it ramps back to 1 while getting up.
    float KnockdownStiffness = 0.15f;
    /// Multiplies the spin a knockdown gets from where the hit landed: a
    /// hit high above the center of mass topples the body backwards, a hit
    /// on the legs sweeps them. 0 is a plain push.
    float KnockdownSpin = 1.0f;
    /// Motor stiffness of a fighter that stays down (knocked out).
    float KnockoutStiffness = 0.05f;
    /// Multiplies the motor stiffness of the legs (the parts posed while
    /// standing) of a knocked-down fighter: 0 lets them buckle, so the body
    /// falls even if the push left it balanced on its spread feet.
    float KnockdownLegStiffness = 0.0f;
    /// @}

    /// \name Two fighters and the walls (rig/spacing.hpp)
    /// @{
    /// A hit landed while the pelvises are closer than this pushes the
    /// fighters apart until they are this far, m.
    float CloseRange = 0.7f;
    /// A standing fighter keeps its pelvis this far from a fighter lying on
    /// the floor, m: its legs do not walk through the body.
    float LyingClearance = 0.2f;
    /// Closer to the wall than this, the fighter touches it, m.
    float WallTouchDistance = 0.03f;
    /// @}

    /// \name Feet
    /// @{
    /// A foot whose sole is this close to the floor is planted: it holds its
    /// place while the pelvis moves, the leg bends to reach it, m.
    float FootPlantHeight = 0.01f;
    /// How far a planted foot may stay away from where the clip puts it; a
    /// longer pull (a knockback) drags it along the floor, m.
    float FootLockSlip = 0.12f;
    /// How fast a lifted foot returns to the clip, 1/s.
    float FootLockRelease = 12.0f;
    /// How fast a planted foot slides along the floor to the clip's pose
    /// when combat lets it (Rig::slideFeet, the end of a walk), m/s.
    float FootSlideSpeed = 2.0f;
    /// A fighter standing still with a planted foot this far from the
    /// stance (left there by a push) steps it back, m; 0 never.
    float FootRestepDistance = 0.05f;
    /// How high a foot stepping back is lifted, per meter it still has to go.
    float FootStepLift = 0.5f;
    /// @}

    /// \name Limbs stuck in the opponent
    /// @{
    /// A limb of the "unjam" list that touches the opponent and is this far
    /// from its target angle for JamSec yields: its motors soften and it
    /// pulls back towards RigDef::YieldAngles, still colliding, rad.
    float JamAngle = 0.35f;
    float JamSec = 0.3f;               ///< s.
    /// Multiplies the motor stiffness of a yielding limb.
    float YieldStiffness = 0.3f;
    /// A limb yields at least this long, and then until the clip's pose of it
    /// is YieldReturnClearance clear of the opponent (or an attack asks it
    /// for something new), s.
    float YieldSec = 0.25f;
    /// How far the clip's pose of a yielding limb must be from every part of
    /// the opponent before it returns, m: larger than a touch, so that a
    /// guard does not come back and jam again while the opponent is close.
    float YieldReturnClearance = 0.05f;
    /// @}
};

/// How the weapon of the loadout is attached to the body (the "weapon"
/// object of a rig file). Its reach comes from the loadout
/// (stats::WeaponProps::ReachM, RigSetup::WeaponReachM).
struct WeaponMount {
    /// The capsule part that holds it; the weapon starts at the part's far
    /// end ("to", the fist) and is a part of it for physics and hits.
    BodyPart Part = BodyPart::ForearmR;
    /// Direction relative to the part's axis (from "from" to "to"), rad;
    /// 0 continues the forearm.
    float Angle = 0.0f;
    float Radius = 0.025f;             ///< Thickness, m.
};

struct RigDef {
    std::vector<PartDef> Parts;    ///< Every body part exactly once.
    std::vector<JointDef> Joints;  ///< Parents before children; every part except the root is a child once.
    BodyPart Root = BodyPart::Pelvis;
    /// Parts that the code moves while the fighter stands (the "kinematic"
    /// list of the file): the root and a chain from it, posed from the clips.
    /// The other parts are physical, driven by joint motors.
    std::bitset<BodyPartCount> Kinematic;
    /// Parts that pass through the same parts of the opponent (the
    /// "passThrough" list of the file): in a side view both fighters' arms
    /// are in one plane, and an arm caught behind the opponent's arm would
    /// jam both. They still collide with every other part. Only physical
    /// parts may pass through.
    std::bitset<BodyPartCount> PassThrough;
    /// Physical parts that yield when they are stuck in the opponent (the
    /// "unjam" list of the file; ControlParams::JamAngle): a chain of them
    /// (upper arm and forearm) softens and pulls back as a whole.
    std::bitset<BodyPartCount> Unjam;
    /// Where a yielding limb pulls back to (the "yieldPose" object of the
    /// file: joint angles in degrees like a clip pose, here in radians, for
    /// a fighter facing right). Joints it leaves out keep the clip's target.
    PerBodyPart<float> YieldAngles{};
    std::bitset<BodyPartCount> YieldPosed;   ///< The joints YieldAngles sets.
    WeaponMount Weapon;
    ControlParams Control;

    const PartDef& getPart(BodyPart Part) const;
};

/// Parses a rig from JSON text. Throws std::runtime_error that names the
/// problem.
RigDef parseRigDef(std::string_view JsonText);

/// Reads and parses a rig file. Throws std::runtime_error.
RigDef loadRigDef(const std::filesystem::path& Path);

} // namespace fighter::rig
