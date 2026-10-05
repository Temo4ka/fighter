#include "rig/spacing.hpp"

#include <algorithm>
#include <format>

#include "debug/draw.hpp"
#include "rig/pelvis_controller.hpp"

namespace fighter::rig {
namespace {

void separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt);
void keepOffLying(Rig& Standing, const Rig& Lying, float MaxX, const SpacingParams& Params, float Dt);

} // namespace

void keepApart(Rig& First, Rig& Second, const SpacingParams& Params, float Dt) {
    const float MaxX = Params.ArenaHalfWidth - Params.BodyHalfWidth;
    const float WallX = Params.ArenaHalfWidth;
    First.updateWallContact(-MaxX, MaxX, WallX);
    Second.updateWallContact(-MaxX, MaxX, WallX);

    const bool FirstUp = First.getPosture() != Posture::KnockedDown;
    const bool SecondUp = Second.getPosture() != Posture::KnockedDown;
    if (FirstUp && SecondUp) {
        separateStanding(First, Second, MaxX, Params, Dt);
    } else if (FirstUp) {
        keepOffLying(First, Second, MaxX, Params, Dt);
    } else if (SecondUp) {
        keepOffLying(Second, First, MaxX, Params, Dt);
    } else {
        return;   // two ragdolls: physics alone
    }
    // The corrections may have taken a pelvis to a wall.
    First.updateWallContact(-MaxX, MaxX, WallX);
    Second.updateWallContact(-MaxX, MaxX, WallX);
}

float pushApartOnHit(Rig& Attacker, Rig& Victim) {
    if (Attacker.getPosture() == Posture::KnockedDown || Victim.getPosture() == Posture::KnockedDown) return 0.0f;
    const float AttackerX = Attacker.getController().getPositionX();
    const float VictimX = Victim.getController().getPositionX();
    const float Deficit = Victim.getControl().CloseRange - std::abs(VictimX - AttackerX);
    if (Deficit <= 0.0f) return 0.0f;

    // Away from the attacker; the victim is in front of it if they stand
    // on the same spot.
    const float Facing = Attacker.isFacingRight() ? 1.0f : -1.0f;
    const float Away = VictimX > AttackerX ? 1.0f : VictimX < AttackerX ? -1.0f : Facing;
    // Split by mass like a collision; a wall behind one gives it all to the other.
    const float AttackerMass = Attacker.getTotalMass();
    float VictimShare = AttackerMass / (AttackerMass + Victim.getTotalMass());
    if (static_cast<float>(Victim.getWallSide()) == Away) {
        VictimShare = 0.0f;
    } else if (static_cast<float>(Attacker.getWallSide()) == -Away) {
        VictimShare = 1.0f;
    }
    Victim.addPush(Away * Deficit * VictimShare);
    Attacker.addPush(-Away * Deficit * (1.0f - VictimShare));
    debug::logEvent(std::format("push-out at close range: {:.2f} m ({:.0f}% the victim)", Deficit,
                                VictimShare * 100.0f));
    return Deficit;
}

namespace {

/// Keeps two standing pelvises 2 * BodyHalfWidth apart.
void separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt) {
    // Who is on the left now: a fighter may have got up on the other side.
    const bool InOrder = First.getController().getPositionX() <= Second.getController().getPositionX();
    Rig& Left = InOrder ? First : Second;
    Rig& Right = InOrder ? Second : First;
    PelvisController& LeftMotion = Left.getController();
    PelvisController& RightMotion = Right.getController();

    // The overlap goes away at a limited speed. Walking into each other is
    // slower than that; a fighter getting up next to the other one is
    // pushed out smoothly instead of jumping.
    const float MinGap = 2.0f * Params.BodyHalfWidth;
    const float Gap = RightMotion.getPlannedX() - LeftMotion.getPlannedX();
    const float Overlap = std::min(MinGap - Gap, Params.SeparationSpeed * Dt);
    if (Overlap <= 0.0f) return;
    const float LeftMass = Left.getTotalMass();
    const float RightMass = Right.getTotalMass();
    LeftMotion.shift(-Overlap * RightMass / (LeftMass + RightMass));
    RightMotion.shift(Overlap * LeftMass / (LeftMass + RightMass));
    LeftMotion.limit(-MaxX, MaxX);
    RightMotion.limit(-MaxX, MaxX);

    // A fighter at a wall cannot give way: the other one takes the rest.
    const float Rest = Gap + Overlap - (RightMotion.getPlannedX() - LeftMotion.getPlannedX());
    if (Rest <= 0.0f) return;
    if (LeftMotion.getPlannedX() <= -MaxX) {
        RightMotion.shift(Rest);
    } else {
        LeftMotion.shift(-Rest);
    }
}

/// Keeps a standing pelvis away from the body of a fighter on the floor.
void keepOffLying(Rig& Standing, const Rig& Lying, float MaxX, const SpacingParams& Params, float Dt) {
    const ExtentX Body = Lying.getExtentX();
    const float Clearance = Params.BodyHalfWidth + Standing.getControl().LyingClearance;
    const float Low = Body.Min - Clearance;
    const float High = Body.Max + Clearance;
    PelvisController& Motion = Standing.getController();
    const float Now = Motion.getPositionX();
    if (Now <= Low) {
        Motion.limit(-MaxX, Low);
    } else if (Now >= High) {
        Motion.limit(High, MaxX);
    } else {
        // The body fell onto the fighter's feet: step out to the nearer side.
        const float Edge = Now - Low <= High - Now ? Low : High;
        const float Step = std::clamp(Edge - Now, -Params.SeparationSpeed * Dt, Params.SeparationSpeed * Dt);
        Motion.shift(Now + Step - Motion.getPlannedX());
        Motion.limit(-MaxX, MaxX);
    }
}

} // namespace

} // namespace fighter::rig
