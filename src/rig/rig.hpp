//===- rig/rig.hpp - Physical body of a fighter -----------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares rig::Rig, the physical body of a fighter: one rigid
/// body per body part, revolute joints between them, and the controller that
/// makes the ragdoll "active" (docs/DEVELOPMENT_PLAN.md, tasks 1.2-1.3):
///  - PD-style joint motors drive every joint towards a target angle;
///  - assists, external forces that are deliberate "cheats" (without them the
///    body falls): an upright torque keeps the pelvis and the torso at their
///    target lean, a height assist keeps the pelvis from sinking into a
///    squat, and a walking force pushes the body to the requested speed;
///  - stiffness scales the motors (torque and response speed): it is raised
///    during own attacks, drops when the fighter is hit and recovers over
///    time. Hits weaken the assists the same way.
///
/// The order of work per simulation step is explicit: set targets, call
/// applyControl(), step the physics world, then report hits with applyHit().
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/body.hpp"
#include "core/vec2.hpp"
#include "physics/body.hpp"
#include "physics/joint.hpp"
#include "physics/world.hpp"
#include "rig/rig_def.hpp"

namespace fighter::rig {

/// Where a rig is created and how strong it is. Masses and motor values come
/// from the fighter's physical profile.
struct RigSetup {
    Vec2 Origin;                  ///< Floor point under the body, m.
    bool FacingRight = true;
    uint8_t FighterIndex = 0;     ///< PartRef::Fighter of the body parts.
    PerBodyPart<float> MassKg{};
    float MotorMaxTorque = 150.0f; ///< N*m for a joint of strength 1 at stiffness 1.
    float MotorGain = 12.0f;       ///< 1/s: motor speed per radian of angle error.
};

/// The physics world owns the bodies and joints: a rig must not outlive it.
class Rig {
public:
    /// Creates the bodies and joints in \p PhysWorld, in the reference pose.
    Rig(physics::World& PhysWorld, const RigDef& Def, const RigSetup& Setup);

    /// \name Control (set before applyControl)
    /// @{
    /// Target joint angles for a fighter facing right (see anim::Pose); the
    /// rig mirrors them when it faces left. The Pelvis entry is the lean.
    void setTargetAngles(const PerBodyPart<float>& Angles);
    /// Requested horizontal speed in the world, m/s; 0 stops the walking force.
    void setMoveVelocity(float Velocity);
    /// Stiffness without hits: 1 normally, higher during an attack.
    void setBaseStiffness(float Stiffness);
    /// @}

    /// Applies motors, the upright torque, the height assist and the walking
    /// force for the next physics step and recovers stiffness after hits.
    void applyControl(float Dt);

    /// The fighter was hit with \p Impulse (N*s): stiffness drops.
    void applyHit(float Impulse);

    /// \name State
    /// @{
    const ControlParams& getControl() const { return Control; }
    bool isFacingRight() const { return Facing > 0.0f; }
    float getStiffness() const { return BaseStiffness * HitFactor; }
    float getTotalMass() const { return TotalMass; }
    /// Motor torque of a joint of strength 1 at stiffness 1, N*m.
    float getMotorMaxTorque() const { return MotorMaxTorque; }
    /// Motor speed per radian of angle error at stiffness 1, 1/s.
    float getMotorGain() const { return MotorGain; }
    /// Upward force of the height assist in the last step, N.
    float getLiftForce() const { return LiftForce; }
    /// Upright assist torques on the pelvis and the torso in the last step, N*m.
    float getPelvisUprightTorque() const { return PelvisUprightTorque; }
    float getTorsoUprightTorque() const { return TorsoUprightTorque; }
    /// Sum of the absolute torques of all joint motors in the last step, N*m.
    float getMotorTorqueSum() const;
    Vec2 getCenterOfMass() const;
    Vec2 getCenterOfMassVelocity() const;
    /// Point on the floor between the feet (lifted when both feet are in the air).
    Vec2 getFloorPoint() const;
    bool isGrounded() const;
    Vec2 getPartPosition(BodyPart Part) const;
    float getPartAngle(BodyPart Part) const;
    /// Angle of the joint whose child is \p Part, unmirrored (as in a pose), rad.
    float getJointAngle(BodyPart Part) const;
    void getPartTransforms(std::vector<PartTransform>& Out) const;
    /// @}

    /// Hurtboxes come from the physics world's debug draw; the rig draws
    /// joint limits, the target pose ghost, motors, forces, velocities and
    /// the center of mass. Does nothing in the release build.
    void drawDebug() const;

private:
    struct PartState {
        PartDef Shape;            ///< Mirrored for the facing, relative to the body origin.
        physics::Body Handle;
        Vec2 Size;                ///< Bounds of the shape in the body frame.
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

    const PartState& getPart(BodyPart Part) const { return Parts[static_cast<size_t>(Part)]; }
    float getUprightTorque(BodyPart Part, float TargetAngle) const;
    /// Height of the lowest corner of a part's bounds, m.
    float getLowestPoint(BodyPart Part) const;
    void drawTargetPose() const;
    void drawJointsAndMotors() const;
    void drawForces() const;

    ControlParams Control;
    BodyPart Root = BodyPart::Pelvis;
    float Facing = 1.0f;          ///< +1 facing right, -1 facing left.
    uint8_t FighterIndex = 0;
    float MotorMaxTorque = 0.0f;
    float MotorGain = 0.0f;
    float TotalMass = 0.0f;

    PerBodyPart<PartState> Parts{};
    std::vector<JointState> Joints;   ///< Parents before children.
    PerBodyPart<float> TargetAngles{};///< As given (unmirrored).

    float MoveVelocity = 0.0f;
    float BaseStiffness = 1.0f;
    float HitFactor = 1.0f;           ///< 1 without hits, drops to MinStiffness.

    /// \name Forces of the last applyControl (for debug drawing)
    /// @{
    float PelvisUprightTorque = 0.0f;
    float TorsoUprightTorque = 0.0f;
    float LiftForce = 0.0f;
    Vec2 WalkForce;
    /// @}
};

} // namespace fighter::rig
