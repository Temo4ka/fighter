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
/// Its motion is the sum of three velocities:
///  - walking: approaches the requested speed with a limited acceleration;
///  - knockback: set by hits (impulse / mass of the fighter) and decaying
///    exponentially;
///  - carry: what the spacing of the fighters pushed it with (setCarry()),
///    going on and easing out at a limited deceleration, so a push never
///    stops dead.
///
/// One step is plan() -> corrections by the battle (arena walls, the other
/// fighter: limit(), shift()) -> commit(). Pure logic, no physics.
///
/// The controller remembers what moved the pelvis in the last step besides
/// its own plan: the spacing of the fighters (shift(), split into taking
/// back the planned approach and pushing further), the walls (limit()) and
/// the push-out of a hit at close range (addPushOut()), for the debug panel
/// and the tests. It also tells which share of the planned travel the
/// pelvis really makes (getTravelShare()): the walk cycle follows it, so
/// the legs step only as far as the pelvis goes.
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
    /// The pelvis goes on at \p Velocity (m/s), slowing down at
    /// \p Deceleration (m/s^2): the push of the spacing (rig::keepApart).
    void setCarry(float NewCarry, float Deceleration) {
        Carry = NewCarry;
        CarryDeceleration = Deceleration;
    }
    float getCarry() const { return Carry; }
    /// The part of getPlannedTravel() that is the carry, m.
    float getCarryTravel() const { return CarryTravel; }
    /// Adds the push-out of a hit at close range (rig::pushApartOnHit), m/s:
    /// knockback that the debug panel shows apart.
    void addPushOut(float Added) {
        Knockback += Added;
        PushOut += Added;
    }

    /// Advances the walking speed and the knockback by \p Dt and plans the
    /// position at the end of the step.
    void plan(float Dt);
    /// Keeps the planned position within [MinX, MaxX]; motion that pushes
    /// further out stops (a wall).
    void limit(float MinX, float MaxX);
    /// Moves the planned position, keeping the velocities (pushed by the
    /// other fighter).
    void shift(float Delta) {
        PlannedX += Delta;
        SpacingShift += Delta;
    }
    /// Makes the planned position the current one.
    void commit(float Dt);
    /// Stands still at \p NewX: no walking, no knockback.
    void reset(float NewX);

    float getPositionX() const { return PositionX; }
    float getPlannedX() const { return PlannedX; }
    /// Actual velocity over the last committed step, after corrections, m/s.
    float getVelocity() const { return Velocity; }
    float getWalkVelocity() const { return WalkVelocity; }
    /// Knockback velocity, the push-out included, m/s.
    float getKnockback() const { return Knockback; }
    /// The part of the knockback that is the push-out (addPushOut()), m/s.
    float getPushOut() const { return PushOut; }
    /// How far plan() moved the planned position in this step (walking and
    /// knockback, before any correction), m.
    float getPlannedTravel() const { return PlannedTravel; }
    /// The share of \p Travel (m, from the current position) that ending
    /// the step at \p EndX makes, in [0, 1]: 0 at the start (or behind it),
    /// 1 at the end of the travel (or beyond it). 1 for no travel.
    float getTravelShare(float EndX, float Travel) const;
    /// The corrections of the planned position in the current step (after
    /// plan(), kept after commit() until the next plan()), m: by shift()
    /// (the spacing of the fighters) and by limit() (the walls).
    float getSpacingShift() const { return SpacingShift; }
    float getWallShift() const { return WallShift; }

    /// What the spacing of the fighters did to this pelvis in the step
    /// (rig::keepApart), m/s along X: Slowed takes back some of the planned
    /// approach (the walk is slower), Pushed moves it beyond its plan (apart
    /// from the opponent; it goes on as the carry).
    struct SpacingMotion {
        float Slowed = 0.0f;
        float Pushed = 0.0f;
        /// How deep the bodies were left in each other (planned), m.
        float Overlap = 0.0f;
    };
    /// rig::keepApart() sets it every step, for the debug panel.
    void setSpacingMotion(SpacingMotion Motion) { Spacing = Motion; }
    SpacingMotion getSpacingMotion() const { return Spacing; }

private:
    float PositionX = 0.0f;
    float PlannedX = 0.0f;
    float Velocity = 0.0f;
    float TargetVelocity = 0.0f;
    float WalkVelocity = 0.0f;
    float Knockback = 0.0f;
    float PushOut = 0.0f;
    float Carry = 0.0f;
    float CarryDeceleration = 0.0f;
    float CarryTravel = 0.0f;
    float PlannedTravel = 0.0f;
    float SpacingShift = 0.0f;
    float WallShift = 0.0f;
    SpacingMotion Spacing;
    Params Config;
};

} // namespace fighter::rig
