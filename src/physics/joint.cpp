#include "physics/joint.hpp"

#include <box2d/box2d.h>

#include "physics/box2d_bridge.hpp"

namespace fighter::physics {

using detail::fromBox2D;
using detail::loadJoint;

bool RevoluteJoint::isValid() const { return Id != 0 && b2Joint_IsValid(loadJoint(Id)); }

float RevoluteJoint::getAngle() const { return b2RevoluteJoint_GetAngle(loadJoint(Id)); }

float RevoluteJoint::getAngularSpeed() const {
    const b2JointId JointId = loadJoint(Id);
    return b2Body_GetAngularVelocity(b2Joint_GetBodyB(JointId)) - b2Body_GetAngularVelocity(b2Joint_GetBodyA(JointId));
}

Vec2 RevoluteJoint::getAnchor() const {
    const b2JointId JointId = loadJoint(Id);
    return fromBox2D(b2Body_GetWorldPoint(b2Joint_GetBodyB(JointId), b2Joint_GetLocalAnchorB(JointId)));
}

float RevoluteJoint::getLowerLimit() const { return b2RevoluteJoint_GetLowerLimit(loadJoint(Id)); }

float RevoluteJoint::getUpperLimit() const { return b2RevoluteJoint_GetUpperLimit(loadJoint(Id)); }

void RevoluteJoint::setMotorSpeed(float Speed) { b2RevoluteJoint_SetMotorSpeed(loadJoint(Id), Speed); }

void RevoluteJoint::setMaxMotorTorque(float Torque) { b2RevoluteJoint_SetMaxMotorTorque(loadJoint(Id), Torque); }

float RevoluteJoint::getMaxMotorTorque() const { return b2RevoluteJoint_GetMaxMotorTorque(loadJoint(Id)); }

float RevoluteJoint::getMotorTorque() const { return b2RevoluteJoint_GetMotorTorque(loadJoint(Id)); }

Body RevoluteJoint::getBodyA() const { return Body(b2StoreBodyId(b2Joint_GetBodyA(loadJoint(Id)))); }

Body RevoluteJoint::getBodyB() const { return Body(b2StoreBodyId(b2Joint_GetBodyB(loadJoint(Id)))); }

} // namespace fighter::physics
