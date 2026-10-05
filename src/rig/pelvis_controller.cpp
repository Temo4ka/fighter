#include "rig/pelvis_controller.hpp"

#include <algorithm>
#include <cmath>

namespace fighter::rig {
namespace {

/// Knockback slower than this is over, m/s.
constexpr float KnockbackRest = 1e-3f;

} // namespace

void PelvisController::plan(float Dt) {
    // Slowing down (the key released, or the other way) uses its own rate.
    const bool Braking = std::abs(TargetVelocity) < std::abs(WalkVelocity) || TargetVelocity * WalkVelocity < 0.0f;
    const float MaxChange = (Braking ? Config.WalkDeceleration : Config.WalkAcceleration) * Dt;
    WalkVelocity += std::clamp(TargetVelocity - WalkVelocity, -MaxChange, MaxChange);
    PlannedX = PositionX + (WalkVelocity + Knockback) * Dt;

    Knockback *= std::exp(-Config.KnockbackDecay * Dt);
    if (std::abs(Knockback) < KnockbackRest) Knockback = 0.0f;
}

void PelvisController::limit(float MinX, float MaxX) {
    if (PlannedX < MinX) {
        PlannedX = MinX;
        WalkVelocity = std::max(WalkVelocity, 0.0f);
        Knockback = std::max(Knockback, 0.0f);
    } else if (PlannedX > MaxX) {
        PlannedX = MaxX;
        WalkVelocity = std::min(WalkVelocity, 0.0f);
        Knockback = std::min(Knockback, 0.0f);
    }
}

void PelvisController::commit(float Dt) {
    Velocity = Dt > 0.0f ? (PlannedX - PositionX) / Dt : 0.0f;
    PositionX = PlannedX;
}

void PelvisController::reset(float NewX) {
    PositionX = NewX;
    PlannedX = NewX;
    Velocity = 0.0f;
    WalkVelocity = 0.0f;
    Knockback = 0.0f;
}

} // namespace fighter::rig
