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
/// A strong hit knocks the fighter down: every part becomes dynamic and the
/// body falls as a ragdoll; after a while the kinematic parts take over again
/// and bring the pelvis back into the stance while the motors ramp up.
///
/// The order of work per simulation step is explicit: set the targets, call
/// planMotion(), let the battle correct the plan (getController()), call
/// applyControl(), step the physics world, then report hits with applyHit().
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
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
};

/// What the body is doing as a whole.
enum class Posture : uint8_t {
    Standing,     ///< Pelvis and legs kinematic, the rest physical.
    KnockedDown,  ///< Every part physical: a ragdoll on the floor.
    GettingUp,    ///< Pelvis and legs return to the stance, motors ramp up.
};

std::string_view getPostureName(Posture State);

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
    /// Places every part in the target pose at rest, standing on the floor.
    /// For the start of a fight; it teleports the bodies.
    void snapToTargets();
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
    /// hit pushes to the right, -1 to the left. Stiffness drops; a standing
    /// fighter gets knockback or, if the hit is strong enough, is knocked down.
    void applyHit(float Impulse, float Direction);
    /// The same, but the caller decides whether the hit knocks the fighter
    /// down (combat's reaction levels, task 2.3): with \p KnockDown a fighter
    /// that is not lying already falls, otherwise it is only pushed back.
    void applyHit(float Impulse, float Direction, bool KnockDown);

    /// \name State
    /// @{
    const ControlParams& getControl() const { return Control; }
    bool isFacingRight() const { return Facing > 0.0f; }
    Posture getPosture() const { return CurrentPosture; }
    /// Time spent in the current posture, s.
    float getPostureSec() const { return PostureSec; }
    /// Is \p Part moved by code right now (not physical)?
    bool isKinematic(BodyPart Part) const;
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
    /// @}

    /// Hurtboxes come from the physics world's debug draw; the rig draws
    /// joint limits, the target pose ghost, motors, velocities (with the
    /// pelvis controller) and the center of mass. Does nothing in the
    /// release build.
    void drawDebug() const;

private:
    struct PartState {
        PartDef Shape;            ///< Mirrored for the facing, relative to the body origin.
        physics::Body Handle;
        Vec2 Size;                ///< Bounds of the shape in the body frame.
        float Mass = 0.0f;        ///< kg, also while the body is kinematic.
        bool Kinematic = false;   ///< Moved by code while the fighter is not knocked down.
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

    /// Where a body origin is and how the body is turned.
    struct Placement {
        Vec2 Position;
        float Angle = 0.0f;
    };

    const PartState& getPart(BodyPart Part) const { return Parts[static_cast<size_t>(Part)]; }
    /// The target pose by forward kinematics from a root placement.
    PerBodyPart<Placement> computeTargetPose(Placement Root) const;
    /// Root placement at the controller position, at the height where the
    /// lowest kinematic part touches the floor.
    Placement getStandingRoot() const;
    float getPostureStiffness() const;
    void moveKinematicParts(float Dt);
    void driveMotors();
    void knockDown(float Velocity);
    void startGettingUp();
    void drawTargetPose() const;
    void drawJointsAndMotors() const;
    void drawController() const;

    physics::World* Physics = nullptr;   ///< Switches parts between kinematic and dynamic.
    ControlParams Control;
    BodyPart Root = BodyPart::Pelvis;
    float Facing = 1.0f;          ///< +1 facing right, -1 facing left.
    uint8_t FighterIndex = 0;
    float MotorMaxTorque = 0.0f;
    float MotorGain = 0.0f;
    float MoveSpeedScale = 1.0f;
    float TotalMass = 0.0f;

    PerBodyPart<PartState> Parts{};
    std::vector<JointState> Joints;   ///< Parents before children.
    PerBodyPart<float> TargetAngles{};///< As given (unmirrored).

    PelvisController Controller;
    Posture CurrentPosture = Posture::Standing;
    float PostureSec = 0.0f;
    /// Kinematic parts as they lay when getting up started.
    PerBodyPart<Placement> GetUpFrom{};

    float BaseStiffness = 1.0f;
    float HitFactor = 1.0f;           ///< 1 without hits, drops to MinStiffness.
};

} // namespace fighter::rig
