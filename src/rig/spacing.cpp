#include "rig/spacing.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <tuple>
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

/// Pushes slower than this are not told in the event log, m/s.
constexpr float MinLoggedPush = 0.05f;

void separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt);
void keepOffLying(Rig& Standing, const Rig& Lying, float MaxX, const SpacingParams& Params, float Dt);
PelvisController::SpacingMotion splitCorrection(const PelvisController& Motion, float Planned, float Corrected,
                                                float Dt);
void reportCorrections(const Rig& Body, float Dt);

} // namespace

void keepApart(Rig& First, Rig& Second, const SpacingParams& Params, float Dt) {
    const float MaxX = Params.ArenaHalfWidth - Params.BodyHalfWidth;
    const float WallX = Params.ArenaHalfWidth;
    First.updateWallContact(-MaxX, MaxX, WallX);
    Second.updateWallContact(-MaxX, MaxX, WallX);

    const bool FirstUp = First.getPosture() != Posture::KnockedDown;
    const bool SecondUp = Second.getPosture() != Posture::KnockedDown;
    // Only two standing fighters push each other (the push eases from the
    // last step's).
    if (!FirstUp || !SecondUp) {
        First.getController().setSpacingMotion({});
        Second.getController().setSpacingMotion({});
    }
    if (FirstUp && SecondUp) {
        separateStanding(First, Second, MaxX, Params, Dt);
    } else if (FirstUp) {
        keepOffLying(First, Second, MaxX, Params, Dt);
    } else if (SecondUp) {
        keepOffLying(Second, First, MaxX, Params, Dt);
    }
    // The corrections may have taken a pelvis to a wall.
    if (FirstUp || SecondUp) {
        First.updateWallContact(-MaxX, MaxX, WallX);
        Second.updateWallContact(-MaxX, MaxX, WallX);
    }
    if constexpr (FIGHTER_DEBUG) {
        reportCorrections(First, Dt);
        reportCorrections(Second, Dt);
    }
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

    // How much closer the plans take the fighters in this step: taking that
    // back only slows the approach down.
    const float Closing = std::max(0.0f, (RightMotion.getPositionX() - LeftMotion.getPositionX()) -
                                             (RightPlanned - LeftPlanned));

    // The shift the plans need: the pelvises 2 * BodyHalfWidth apart, and
    // the bodies off each other where they will stand after this step (see
    // the file comment; a walk the shift holds back steps less far,
    // Rig::setTravelPose).
    const float MinGap = 2.0f * Params.BodyHalfWidth;
    const float PelvisNeed = std::max(0.0f, MinGap - (RightPlanned - LeftPlanned));
    const float MaxShift = std::max(PelvisNeed, Params.PosedSeparationSpeed * Dt);
    const auto getOverlap = [&](float Trial) {
        const auto [LeftX, RightX] = place(Trial);
        return -Left.measureGap(Left.predictBody(LeftX, Dt), Right.predictBody(RightX, Dt));
    };
    const float Overlap = getOverlap(PelvisNeed);
    float Needed = PelvisNeed;
    if (Overlap > 0.0f) {
        const float OverlapApart = getOverlap(MaxShift);
        if (OverlapApart <= 0.0f) {
            // The smallest shift that takes them apart: Low overlaps, High
            // does not.
            float Low = PelvisNeed;
            float High = MaxShift;
            for (int Step = 0; Step < SeparationSearchSteps; ++Step) {
                const float Middle = (Low + High) * 0.5f;
                (getOverlap(Middle) > 0.0f ? Low : High) = Middle;
            }
            Needed = High;
        } else if (OverlapApart < Overlap - MinUsefulShift) {
            Needed = MaxShift;   // more than a step can do: push as fast as allowed
        } else {
            // Moving apart does not take them apart (two planted feet): no
            // push for nothing, but no closer either.
            Needed = std::max(PelvisNeed, Closing);
        }
    }

    // Taking back the approach is free: the walk just slows down. What is
    // left pushes the fighters apart. A push goes on as the pelvises' own
    // motion (the carry, PelvisController::setCarry), easing out at
    // PushAcceleration, and grows by PushAcceleration at most, up to
    // PushMaxSpeed: no jerk, no back and forth.
    const float Slowed = std::min(Needed, Closing);
    // The push of the last step goes on in this step's plans (the carry,
    // already eased out by PushAcceleration * Dt); a new push may make up for
    // that and grow by as much again, up to PushMaxSpeed.
    const float Carried = std::max(0.0f, RightMotion.getCarry() - LeftMotion.getCarry());
    const float MaxPush =
        std::clamp(Params.PushMaxSpeed - Carried, 0.0f, 2.0f * Params.PushAcceleration * Dt) * Dt;
    float Pushed = std::clamp(Needed - Slowed, 0.0f, MaxPush);
    // Nothing passes through the opponent: bodies that the eased push would
    // leave deeper than MaxSoftOverlap in each other (a kick, a crouch, a
    // foot set down on the opponent's) are pushed as far as that needs, at
    // once (a hard push, told in the event log).
    // An overlap left over from the last step (two feet that met) only has
    // to shrink: it is pushed out eased.
    const float LeftOver = std::min(LeftMotion.getSpacingMotion().Overlap, RightMotion.getSpacingMotion().Overlap);
    const float MaxOverlap = std::max(Params.MaxSoftOverlap, LeftOver);
    bool Hard = false;
    float OverlapLeft = Overlap > 0.0f ? getOverlap(Slowed + Pushed) : 0.0f;
    if (OverlapLeft > MaxOverlap) {
        float Low = Slowed + Pushed;
        float High = std::max(Low, Needed);
        if (getOverlap(High) <= MaxOverlap) {
            for (int Step = 0; Step < SeparationSearchSteps; ++Step) {
                const float Middle = (Low + High) * 0.5f;
                (getOverlap(Middle) > MaxOverlap ? Low : High) = Middle;
            }
        }
        Pushed = High - Slowed;
        OverlapLeft = getOverlap(High);
        Hard = true;
    }
    const float Shift = Slowed + Pushed;
    if constexpr (FIGHTER_DEBUG) {
        debug::setPanel("spacing", std::format("{} needs {:.3f} m: slows {:.3f}, pushes {:.3f} m ({:.2f} m/s){}",
                                               Overlap > 0.0f ? std::format("bodies overlap {:.3f} m,", Overlap)
                                                              : std::string("pelvises"),
                                               Needed, Slowed, Pushed, Pushed / Dt,
                                               Hard                              ? ", hard"
                                               : Needed - Slowed > MaxPush + 1e-6f ? ", eased"
                                                                                   : ""));
        if (Hard) {
            debug::logEvent(std::format("spacing hard push: bodies overlap {:.3f} m, apart {:.3f} m at once", Overlap,
                                        Pushed));
        } else if (Carried < MinLoggedPush && Pushed / Dt >= MinLoggedPush) {
            debug::logEvent(std::format("spacing pushes apart: {}needs {:.3f} m more than the approach",
                                        Overlap > 0.0f ? std::format("bodies overlap {:.3f} m, ", Overlap) : "",
                                        Needed - Slowed));
        } else if (Carried >= MinLoggedPush && Pushed / Dt < MinLoggedPush) {
            debug::logEvent("spacing push over");
        }
    }
    // The push goes on and eases out (the carry); the slowing does not: a
    // fighter walked into stops with the walk.
    const auto [LeftX, RightX] = place(Shift);
    const auto [LeftSlowX, RightSlowX] = place(Slowed);
    for (auto&& [Motion, Planned, Corrected, SlowedTo] :
         {std::tuple(&LeftMotion, LeftPlanned, LeftX, LeftSlowX),
          std::tuple(&RightMotion, RightPlanned, RightX, RightSlowX)}) {
        // A hard push does not carry on faster than an eased one.
        const float NewCarry = std::clamp(Motion->getCarry() + (Corrected - SlowedTo) / Dt, -Params.PushMaxSpeed,
                                          Params.PushMaxSpeed);
        Motion->setCarry(NewCarry, Params.PushAcceleration);
        const PelvisController::SpacingMotion Split = splitCorrection(*Motion, Planned, Corrected, Dt);
        Motion->setSpacingMotion({.Slowed = Split.Slowed, .Pushed = Split.Pushed, .Overlap = OverlapLeft});
    }
    if (Shift <= 0.0f) return;
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

/// The correction of a pelvis planned at \p Planned to \p Corrected, as
/// speeds: the part that takes back its own planned travel, and the rest.
PelvisController::SpacingMotion splitCorrection(const PelvisController& Motion, float Planned, float Corrected,
                                                float Dt) {
    const float Start = Motion.getPositionX();
    const float Kept = std::clamp(Corrected, std::min(Start, Planned), std::max(Start, Planned));
    return {.Slowed = (Kept - Planned) / Dt, .Pushed = (Corrected - Kept) / Dt};
}

/// The panel line "P1 push" and the arrows of what moved the pelvis in this
/// step besides its own plan: the spacing (slowed, pushed), the wall, the
/// push-out of a hit at close range.
void reportCorrections(const Rig& Body, float Dt) {
    const std::string Name = std::format("P{} push", Body.getFighterIndex() + 1);
    if (Body.getPosture() == Posture::KnockedDown) {
        debug::setPanel(Name, "ragdoll");
        return;
    }
    const PelvisController& Motion = Body.getController();
    const PelvisController::SpacingMotion Spacing = Motion.getSpacingMotion();
    const float Wall = Motion.getWallShift() / Dt;
    const float PushOut = Motion.getPushOut();
    debug::setPanel(Name, std::format("spacing: slowed {:+.2f}, pushed {:+.2f}; wall {:+.2f}; push-out {:+.2f} m/s",
                                      Spacing.Slowed, Spacing.Pushed, Wall, PushOut));
    // Arrows above the pelvis, one row per source.
    constexpr float ArrowScale = 0.2f;   // m per m/s
    constexpr float MinDrawn = 0.01f;    // m/s
    const Vec2 Pelvis = Body.getPartPosition(BodyPart::Pelvis);
    const std::array Rows = {std::pair{Spacing.Slowed, "slowed"}, std::pair{Spacing.Pushed, "pushed"},
                             std::pair{Wall, "wall"}, std::pair{PushOut, "push-out"}};
    float Height = 0.25f;
    for (const auto& [Speed, Label] : Rows) {
        if (std::abs(Speed) >= MinDrawn) {
            debug::drawArrow(debug::Cat::Forces, Pelvis + Vec2{0.0f, Height}, {Speed * ArrowScale, 0.0f},
                             std::format("{} {:+.2f}", Label, Speed));
        }
        Height += 0.06f;
    }
}

} // namespace

} // namespace fighter::rig
