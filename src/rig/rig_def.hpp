//===- rig/rig_def.hpp - Rig description loaded from JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares RigDef, the description of a fighter's body that is
/// loaded from data/rigs/<id>.json: body parts and their shapes, joints with
/// angle limits, and the parameters of the balance and motor controller.
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

/// Parameters of the controller that keeps the fighter standing and moving
/// (the "control" object of a rig file; docs/TUNING.md describes each one).
/// The "assist" forces (upright torque, height assist, walking force) act on
/// the body from outside; they are what makes an active ragdoll playable.
struct ControlParams {
    /// \name Motors
    /// @{
    float TorqueScale = 1.0f;          ///< Multiplies the profile's motor torque.
    float GainScale = 1.0f;            ///< Multiplies the profile's motor gain.
    float MaxJointSpeed = 15.0f;       ///< Motor speed limit, rad/s.
    float AngularDamping = 0.5f;       ///< Of every body part, 1/s.
    /// A foot whose lowest corner is below this height keeps its sole flat
    /// instead of following the clip, m; 0 turns foot levelling off.
    float FootLevelHeight = 0.03f;
    /// @}

    /// \name Balance assist
    /// @{
    float UprightStiffness = 2000.0f;  ///< Torque per radian of lean error, N*m/rad.
    float UprightDamping = 200.0f;     ///< N*m*s/rad.
    float UprightTorqueLimit = 1500.0f;///< N*m, per stabilized part (pelvis, torso).
    /// Height assist: an upward force on the pelvis when it sinks below the
    /// standing height while a foot is on the floor. It keeps the legs from
    /// folding into a squat the motors cannot get out of.
    float StandHeight = 0.93f;         ///< Pelvis center height, m.
    float HeightStiffness = 4000.0f;   ///< N/m.
    float HeightDamping = 300.0f;      ///< N*s/m.
    float HeightForceLimit = 400.0f;   ///< N, never pulls down.
    /// A foot whose sole is this close to the floor counts as standing on it;
    /// the assists and the walking force work only while grounded, m.
    float GroundTolerance = 0.05f;
    /// @}

    /// \name Walking
    /// @{
    float WalkSpeed = 1.5f;            ///< m/s.
    float BackwardSpeedScale = 0.7f;   ///< Walking backwards is slower.
    float WalkForceGain = 300.0f;      ///< N per m/s of speed error.
    float WalkForceLimit = 400.0f;     ///< N.
    /// The walk cycle plays at the rate of the distance covered; while the
    /// first step starts it plays at least this fast (a share of the normal rate).
    float WalkCycleMinRate = 0.7f;
    float WalkStartSec = 0.4f;         ///< How long the first step lasts, s.
    /// @}

    /// \name Stiffness after hits
    /// @{
    float MinStiffness = 0.2f;         ///< Lowest stiffness after a hit.
    float StiffnessPerImpulse = 0.03f; ///< Stiffness lost per N*s of hit impulse.
    float StiffnessRecovery = 1.0f;    ///< Stiffness regained per second.
    /// @}
};

struct RigDef {
    std::vector<PartDef> Parts;    ///< Every body part exactly once.
    std::vector<JointDef> Joints;  ///< Parents before children; every part except the root is a child once.
    BodyPart Root = BodyPart::Pelvis;
    ControlParams Control;

    const PartDef& getPart(BodyPart Part) const;
};

/// Parses a rig from JSON text. Throws std::runtime_error that names the
/// problem.
RigDef parseRigDef(std::string_view JsonText);

/// Reads and parses a rig file. Throws std::runtime_error.
RigDef loadRigDef(const std::filesystem::path& Path);

} // namespace fighter::rig
