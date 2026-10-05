#include "rig/spacing.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "debug/draw.hpp"
#include "rig/pelvis_controller.hpp"

namespace fighter::rig {
namespace {

/// Bisection steps of the shift that takes the posed parts apart: to a
/// few micrometres.
constexpr int SeparationSearchSteps = 14;
/// Posed parts that the largest shift of a step takes apart by less than
/// this are not pushed apart (planted feet do not move with the pelvis), m.
constexpr float MinUsefulShift = 0.001f;

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

/// Keeps two standing fighters apart: their pelvises 2 * BodyHalfWidth, and
/// their posed parts (the legs) off each other.
void separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt) {
    // Who is on the left now: a fighter may have got up on the other side.
    const bool InOrder = First.getController().getPositionX() <= Second.getController().getPositionX();
    Rig& Left = InOrder ? First : Second;
    Rig& Right = InOrder ? Second : First;
    PelvisController& LeftMotion = Left.getController();
    PelvisController& RightMotion = Right.getController();
    const float LeftPlanned = LeftMotion.getPlannedX();
    const float RightPlanned = RightMotion.getPlannedX();

    // Where the pelvises end up when the fighters move \p Shift apart: split
    // by mass (the heavier one gives way less), and a fighter at a wall
    // cannot give way, the other one takes the rest.
    const float LeftShare = Right.getTotalMass() / (Left.getTotalMass() + Right.getTotalMass());
    const auto place = [&](float Shift) {
        float LeftX = LeftPlanned - Shift * LeftShare;
        float RightX = RightPlanned + Shift * (1.0f - LeftShare);
        if (LeftX < -MaxX) {
            RightX += -MaxX - LeftX;
            LeftX = -MaxX;
        }
        if (RightX > MaxX) {
            LeftX = std::max(LeftX - (RightX - MaxX), -MaxX);
            RightX = MaxX;
        }
        return std::pair(LeftX, RightX);
    };

    // The overlap goes away at a limited speed. Walking into each other is
    // slower than that; a fighter getting up next to the other one is
    // pushed out smoothly instead of jumping.
    const float MinGap = 2.0f * Params.BodyHalfWidth;
    float Shift = std::clamp(MinGap - (RightPlanned - LeftPlanned), 0.0f, Params.SeparationSpeed * Dt);

    // The bodies keep off each other where they will stand after this step
    // (see the file comment). A pair that moving apart does not take apart
    // (two planted feet) is not pushed for nothing.
    const float MaxShift = std::max(Shift, Params.PosedSeparationSpeed * Dt);
    const auto getOverlap = [&](float Trial) {
        const auto [LeftX, RightX] = place(Trial);
        return -Left.measureGap(Left.predictBody(LeftX, Dt), Right.predictBody(RightX, Dt));
    };
    const float Overlap = getOverlap(Shift);
    if (Overlap > 0.0f) {
        const float OverlapApart = getOverlap(MaxShift);
        if (OverlapApart <= 0.0f) {
            // The smallest shift that takes them apart: Low overlaps, High
            // does not.
            float Low = Shift;
            float High = MaxShift;
            for (int Step = 0; Step < SeparationSearchSteps; ++Step) {
                const float Middle = (Low + High) * 0.5f;
                (getOverlap(Middle) > 0.0f ? Low : High) = Middle;
            }
            Shift = High;
        } else if (OverlapApart < Overlap - MinUsefulShift) {
            Shift = MaxShift;
        }
    }
    if constexpr (FIGHTER_DEBUG) {
        debug::setPanel("spacing", Overlap > 0.0f ? std::format("bodies overlap {:.3f} m -> apart {:.3f} m", Overlap,
                                                                Shift)
                                                  : std::format("pelvises apart {:.3f} m", Shift));
    }
    if (Shift <= 0.0f) return;
    const auto [LeftX, RightX] = place(Shift);
    Left.pushBody(LeftX - LeftPlanned);
    Right.pushBody(RightX - RightPlanned);
    LeftMotion.limit(-MaxX, MaxX);
    RightMotion.limit(-MaxX, MaxX);
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
