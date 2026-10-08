#include "rig/pelvis_controller.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fighter::rig {
namespace {

/// Knockback slower than this is over, m/s.
constexpr float KnockbackRest = 1e-3f;
/// A travel shorter than this is no travel (getTravelShare()), m.
constexpr float MinTravel = 1e-6f;

} // namespace

void PelvisController::plan(float Dt) {
    // Slowing down (the key released, or the other way) uses its own rate.
    const bool Braking = std::abs(TargetVelocity) < std::abs(WalkVelocity) || TargetVelocity * WalkVelocity < 0.0f;
    const float MaxChange = (Braking ? Config.WalkDeceleration : Config.WalkAcceleration) * Dt;
    WalkVelocity += std::clamp(TargetVelocity - WalkVelocity, -MaxChange, MaxChange);
    PlannedX = PositionX + (WalkVelocity + Knockback) * Dt + ClipTravel;
    PlannedTravel = PlannedX - PositionX;
    PlannedClipTravel = std::exchange(ClipTravel, 0.0f);
    SpacingShift = 0.0f;
    WallShift = 0.0f;

    const float Decay = std::exp(-Config.KnockbackDecay * Dt);
    Knockback *= Decay;
    PushOut *= Decay;
    if (std::abs(Knockback) < KnockbackRest) Knockback = 0.0f;
    if (std::abs(PushOut) < KnockbackRest) PushOut = 0.0f;
}

float PelvisController::getTravelShare(float EndX, float Travel) const {
    if (std::abs(Travel) < MinTravel) return 1.0f;
    return std::clamp((EndX - PositionX) / Travel, 0.0f, 1.0f);
}

void PelvisController::limit(float MinX, float MaxX) {
    if (PlannedX < MinX) {
        WallShift += MinX - PlannedX;
        PlannedX = MinX;
        WalkVelocity = std::max(WalkVelocity, 0.0f);
        Knockback = std::max(Knockback, 0.0f);
        PushOut = std::max(PushOut, 0.0f);
    } else if (PlannedX > MaxX) {
        WallShift += MaxX - PlannedX;
        PlannedX = MaxX;
        WalkVelocity = std::min(WalkVelocity, 0.0f);
        Knockback = std::min(Knockback, 0.0f);
        PushOut = std::min(PushOut, 0.0f);
    }
}

void PelvisController::slowWalk(float Speed) {
    const float Along = WalkVelocity >= 0.0f ? 1.0f : -1.0f;
    WalkVelocity = Along * std::clamp(Speed * Along, 0.0f, std::abs(WalkVelocity));
}

bool PelvisController::capWalkTravel(float MaxTravel, float Dt) {
    const float WalkTravel = WalkVelocity * Dt;
    const float Limit = std::max(MaxTravel, 0.0f);
    if (std::abs(WalkTravel) <= Limit) return false;
    const float Kept = std::copysign(Limit, WalkTravel);
    PlannedX += Kept - WalkTravel;
    PlannedTravel = PlannedX - PositionX;
    WalkVelocity = 0.0f;
    return true;
}

void PelvisController::commit(float Dt) {
    // The clip's travel makes the share of the plan the pelvis makes.
    ClipTravelMade = PlannedClipTravel * getTravelShare(PlannedX, PlannedTravel);
    ClipVelocity = Dt > 0.0f ? ClipTravelMade / Dt : 0.0f;
    Velocity = Dt > 0.0f ? (PlannedX - PositionX) / Dt : 0.0f;
    PositionX = PlannedX;
}

void PelvisController::reset(float NewX) {
    PositionX = NewX;
    PlannedX = NewX;
    Velocity = 0.0f;
    WalkVelocity = 0.0f;
    Knockback = 0.0f;
    PushOut = 0.0f;
    PlannedTravel = 0.0f;
    ClipTravel = 0.0f;
    PlannedClipTravel = 0.0f;
    ClipTravelMade = 0.0f;
    ClipVelocity = 0.0f;
    SpacingShift = 0.0f;
    WallShift = 0.0f;
    Spacing = {};
}

} // namespace fighter::rig
