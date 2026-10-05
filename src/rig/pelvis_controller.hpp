//===- rig/pelvis_controller.hpp - Pelvis motion of a fighter ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares rig::PelvisController, the character controller of the
/// hybrid body (docs/DEVELOPMENT_PLAN.md, task 1.5.1): where the pelvis of a
/// standing fighter is along the arena. The pelvis is a kinematic body, so
/// this controller, not a force, decides how it moves.
///
/// Its motion is the sum of two velocities:
///  - walking: approaches the requested speed with a limited acceleration;
///  - knockback: set by hits (impulse / mass of the fighter) and decaying
///    exponentially.
///
/// One step is plan() -> corrections by the battle (arena walls, the other
/// fighter: limit(), shift()) -> commit(). Pure logic, no physics.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace fighter::rig {

class PelvisController {
public:
    struct Params {
        float WalkAcceleration = 8.0f;   ///< m/s^2.
        /// Slowing down (towards a slower or no walk), m/s^2.
        float WalkDeceleration = 8.0f;
        float KnockbackDecay = 4.0f;     ///< 1/s.
    };

    PelvisController() = default;
    PelvisController(float StartX, Params Settings) : PositionX(StartX), PlannedX(StartX), Config(Settings) {}

    /// Walking speed the fighter wants, m/s (positive is to the right).
    void setTargetVelocity(float Requested) { TargetVelocity = Requested; }
    /// Adds the knockback of a hit, m/s.
    void addKnockback(float Added) { Knockback += Added; }

    /// Advances the walking speed and the knockback by \p Dt and plans the
    /// position at the end of the step.
    void plan(float Dt);
    /// Keeps the planned position within [MinX, MaxX]; motion that pushes
    /// further out stops (a wall).
    void limit(float MinX, float MaxX);
    /// Moves the planned position, keeping the velocities (pushed by the
    /// other fighter).
    void shift(float Delta) { PlannedX += Delta; }
    /// Makes the planned position the current one.
    void commit(float Dt);
    /// Stands still at \p NewX: no walking, no knockback.
    void reset(float NewX);

    float getPositionX() const { return PositionX; }
    float getPlannedX() const { return PlannedX; }
    /// Actual velocity over the last committed step, after corrections, m/s.
    float getVelocity() const { return Velocity; }
    float getWalkVelocity() const { return WalkVelocity; }
    float getKnockback() const { return Knockback; }

private:
    float PositionX = 0.0f;
    float PlannedX = 0.0f;
    float Velocity = 0.0f;
    float TargetVelocity = 0.0f;
    float WalkVelocity = 0.0f;
    float Knockback = 0.0f;
    Params Config;
};

} // namespace fighter::rig
