//===- physics/joint.hpp - Revolute joints ----------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares physics::RevoluteJoint, a non-owning handle to a hinge
/// between two bodies, and RevoluteJointDef, used to create one.
///
/// The joint angle is the angle of body B relative to body A, measured from
/// their relative angle at the moment the joint was created. Its motor is a
/// velocity motor: it drives the joint towards MotorSpeed with at most
/// MaxMotorTorque. A PD-style controller on top of it lives in rig.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "core/vec2.hpp"
#include "physics/body.hpp"

namespace fighter::physics {

struct RevoluteJointDef {
    Body BodyA;                  ///< Parent.
    Body BodyB;                  ///< Child.
    Vec2 Anchor;                 ///< World point of the hinge at creation, m.
    bool EnableLimit = true;
    float LowerAngle = 0.0f;     ///< rad, >= -0.99 pi.
    float UpperAngle = 0.0f;     ///< rad, <= 0.99 pi.
    bool EnableMotor = true;
    float MaxMotorTorque = 0.0f; ///< N*m.
    float MotorSpeed = 0.0f;     ///< rad/s.
};

/// Non-owning handle to a revolute joint of a World.
class RevoluteJoint {
public:
    RevoluteJoint() = default;

    bool isValid() const;

    float getAngle() const;          ///< rad, relative to the angle at creation.
    float getAngularSpeed() const;   ///< rad/s, of body B relative to body A.
    Vec2 getAnchor() const;          ///< Current world position of the hinge on body B, m.
    float getLowerLimit() const;
    float getUpperLimit() const;

    void setMotorSpeed(float Speed);
    void setMaxMotorTorque(float Torque);
    float getMaxMotorTorque() const;
    /// Torque the motor applied during the last step, N*m.
    float getMotorTorque() const;

    Body getBodyA() const;
    Body getBodyB() const;

private:
    friend class World;

    explicit RevoluteJoint(uint64_t Packed) : Id(Packed) {}

    uint64_t Id = 0;   ///< b2JointId packed with b2StoreJointId; 0 is null.
};

} // namespace fighter::physics
