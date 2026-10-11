//===- editor/arm_ik.hpp - Inverse kinematics of an arm ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the inverse kinematics of the pose editor: the joint
/// angles that put a fist on a point (an analytic two-bone solution for the
/// upper arm and the forearm), the wrist angle that points a weapon at a
/// point, the handle point the other hand grips on a two-handed weapon, and
/// turning one joint by a drag about its pivot.
///
/// Everything works on a body posed by poseBody() (world placement of the
/// parent parts) and returns joint angles, rad, always within the rig limits:
/// a point the arm cannot reach makes it reach as close as it can.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "core/body.hpp"
#include "core/vec2.hpp"
#include "editor/pose_fk.hpp"
#include "rig/rig_def.hpp"

namespace fighter::editor {

/// Which way the elbow bends. Down: the forearm joint angle is positive, the
/// forearm turns counter-clockwise from the upper arm (for a fighter facing
/// right with the arm reaching forward the elbow is below the line from the
/// shoulder to the fist; the natural bend of the humanoid rig). Up: negative.
enum class ElbowBend : uint8_t {
    Down,
    Up,
};

/// The joint angles of an arm, rad: the upper arm to its parent, the forearm
/// to the upper arm.
struct ArmAngles {
    float Upper = 0.0f;
    float Forearm = 0.0f;
};

/// An arm solved for a target.
struct ArmSolution {
    ArmAngles Angles;
    ElbowBend Bend = ElbowBend::Down;   ///< The bend the solution has.
    Vec2 Fist;                          ///< Where the fist ends up.
    bool Reached = false;               ///< The fist is on the target (within 1 mm).
};

/// The wrist and the arm after pointing a weapon at a target.
struct WeaponAim {
    float Wrist = 0.0f;   ///< rad, to the forearm.
    ArmAngles Arm;
};

/// The world position of the joint pivot of \p Part (its anchor on the
/// parent) in \p Body; for the root its origin.
Vec2 getJointPivot(const rig::RigDef& Def, const PosedBody& Body, BodyPart Part);

/// The fist of \p Forearm in \p Body: the far end of its capsule.
Vec2 getFistPoint(const rig::RigDef& Def, const PosedBody& Body, BodyPart Forearm);

/// The angle of the joint of \p Part in \p Body (after the limits of
/// poseBody()): its turn from its parent; for the root its own angle.
float getPosedJointAngle(const rig::RigDef& Def, const PosedBody& Body, BodyPart Part);

/// \p Angle (rad) moved into \p Range: kept if inside (or inside after a full
/// turn either way), else the limit nearer by turn.
float clampJointAngle(float Angle, AngleRange Range);

/// The arm whose forearm is \p Forearm put with its fist on \p Target; the
/// parent of the upper arm stays as \p Body has it.
///
/// Both bends are solved and clamped to the limits: the forearm angle first,
/// then the upper arm turned to aim that fist at the target. The \p Preferred
/// bend wins unless the other one puts the fist closer to the target (by more
/// than 1 mm): with the humanoid's elbow (0..150 degrees) Up is never possible,
/// so it falls back to Down. A target out of reach stretches the arm towards
/// it.
ArmSolution solveArmIk(const rig::RigDef& Def, const PosedBody& Body, BodyPart Forearm, Vec2 Target,
                       ElbowBend Preferred);

/// Points \p Weapon of \p Body at \p Target by the shortest turn. The wrist
/// turns first, within its limits; the turn it cannot make goes to the elbow,
/// and what the elbow cannot make to the shoulder (the fist then moves). The
/// blade points at the target unless every joint is at its limit.
WeaponAim aimWeapon(const rig::RigDef& Def, const PosedBody& Body, const PosedWeapon& Weapon, Vec2 Target);

/// The point of \p Weapon the other hand grips: \p GripM along the weapon
/// from the fist (negative: behind it), no farther than the tip, as the rig
/// places its grip spring (rig::Rig::createGrip).
Vec2 getGripPoint(const PosedWeapon& Weapon, float GripM);

/// The angle of a joint after a drag from \p From to \p To about its pivot
/// \p Pivot: \p StartAngle plus the turn of the cursor about the
/// pivot, clamped to \p Range.
float dragJointAngle(float StartAngle, Vec2 Pivot, Vec2 From, Vec2 To, AngleRange Range);

} // namespace fighter::editor
