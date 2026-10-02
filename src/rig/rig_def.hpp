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

    /// \name Pelvis controller
    /// @{
    float WalkSpeed = 1.2f;            ///< m/s; the profile's MoveSpeedScale (DEX) multiplies it.
    float BackwardSpeedScale = 0.7f;   ///< Walking backwards is slower.
    float WalkAcceleration = 8.0f;     ///< How fast the walking speed is gained and lost, m/s^2.
    /// @}

    /// \name Hit reaction
    /// @{
    float MinStiffness = 0.25f;        ///< Lowest stiffness after a hit.
    float StiffnessPerImpulse = 0.03f; ///< Stiffness lost per N*s of hit impulse.
    float StiffnessRecovery = 0.8f;    ///< Stiffness regained per second.
    /// Multiplies the knockback speed, impulse / mass of the whole fighter.
    float KnockbackScale = 1.0f;
    float KnockbackDecay = 5.0f;       ///< Exponential decay of the knockback speed, 1/s.
    /// @}

    /// \name Knockdown
    /// @{
    /// A hit whose knockback speed (impulse / mass) reaches this knocks the
    /// fighter down: the threshold impulse grows with the fighter's mass, m/s.
    float KnockdownSpeed = 1.5f;
    float KnockdownSec = 1.5f;         ///< Time on the floor, s.
    float GetUpSec = 0.8f;             ///< Time to get back into the stance, s.
    /// Motor stiffness while down; it ramps back to 1 while getting up.
    float KnockdownStiffness = 0.15f;
    /// @}
};

struct RigDef {
    std::vector<PartDef> Parts;    ///< Every body part exactly once.
    std::vector<JointDef> Joints;  ///< Parents before children; every part except the root is a child once.
    BodyPart Root = BodyPart::Pelvis;
    /// Parts that the code moves while the fighter stands (the "kinematic"
    /// list of the file): the root and a chain from it, posed from the clips.
    /// The other parts are physical, driven by joint motors.
    std::bitset<BodyPartCount> Kinematic;
    ControlParams Control;

    const PartDef& getPart(BodyPart Part) const;
};

/// Parses a rig from JSON text. Throws std::runtime_error that names the
/// problem.
RigDef parseRigDef(std::string_view JsonText);

/// Reads and parses a rig file. Throws std::runtime_error.
RigDef loadRigDef(const std::filesystem::path& Path);

} // namespace fighter::rig
