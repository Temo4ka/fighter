#include "rig/spacing.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

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

SpacingReport separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt);
float keepOffLying(Rig& Standing, const Rig& Lying, float MaxX, const SpacingParams& Params, float Dt);
PelvisController::SpacingMotion splitCorrection(const PelvisController& Motion, float Planned, float Corrected,
                                                float Dt);
// Only the debug build fills the panel.
[[maybe_unused]] void reportCorrections(const Rig& Body, float Dt);

} // namespace

SpacingReport keepApart(Rig& First, Rig& Second, const SpacingParams& Params, float Dt) {
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
    SpacingReport Report;
    if (FirstUp && SecondUp) {
        Report = separateStanding(First, Second, MaxX, Params, Dt);
    } else if (FirstUp) {
        Report.OffLying[0] = keepOffLying(First, Second, MaxX, Params, Dt);
    } else if (SecondUp) {
        Report.OffLying[1] = keepOffLying(Second, First, MaxX, Params, Dt);
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
    return Report;
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
SpacingReport separateStanding(Rig& First, Rig& Second, float MaxX, const SpacingParams& Params, float Dt) {
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

    // Taking back the approach is free: the walk just slows down (and a
    // fighter walked into is shoved along, by mass). What is left pushes the
    // fighters apart: that push speed grows by PushAcceleration at most, up
    // to PushMaxSpeed, and it stops where the bodies are apart (it only does
    // what they need, so it slows down to their contact): no jerk, no back
    // and forth.
    const float Slowed = std::min(Needed, Closing);
    const float LastPush =
        std::max(0.0f, RightMotion.getSpacingMotion().Eased - LeftMotion.getSpacingMotion().Eased);
    const float MaxPush = std::min(Params.PushMaxSpeed, LastPush + Params.PushAcceleration * Dt) * Dt;
    float Pushed = std::clamp(Needed - Slowed, 0.0f, MaxPush);
    // Nothing passes through the opponent: bodies that the eased push would
    // leave deeper than MaxSoftOverlap in each other (a kick, a crouch, a
    // foot set down on the opponent's) are pushed apart at once, as far as
    // they need (a hard push, told in the event log).
    // An overlap left over from the last step (two feet that met) only has
    // to shrink: it is pushed out eased.
    const float LeftOver = std::min(LeftMotion.getSpacingMotion().Overlap, RightMotion.getSpacingMotion().Overlap);
    // A strike sweeps fast: while one swings, overlaps are not eased (its
    // stop at the contact needs the bodies where they are meant to be).
    const bool Striking = Left.isStriking() || Right.isStriking();
    const float SoftOverlap = std::max(Params.MaxSoftOverlap, LeftOver);
    // While a strike swings, the striking limb is not eased off at all: its
    // stop at the contact (Rig::stopPosedLimbs) needs the opponent where it
    // is meant to be. Other overlaps during a strike (a leg set down by a
    // switch-step, a guard, a lean) are eased as ever: a posed leg that
    // swung into the opponent is held back there (Rig::stopPosedLimbs), the
    // torsos and heads are kept apart by the solver.
    const auto getStrikerOverlap = [&](float Trial) {
        const auto [LeftX, RightX] = place(Trial);
        const std::vector<PartPlacement> LeftBody = Left.predictBody(LeftX, Dt);
        const std::vector<PartPlacement> RightBody = Right.predictBody(RightX, Dt);
        std::vector<PartPlacement> LeftStrikers;
        std::vector<PartPlacement> RightStrikers;
        std::ranges::copy_if(LeftBody, std::back_inserter(LeftStrikers), &PartPlacement::Striking);
        std::ranges::copy_if(RightBody, std::back_inserter(RightStrikers), &PartPlacement::Striking);
        return -std::min(Left.measureGap(LeftStrikers, RightBody), Left.measureGap(LeftBody, RightStrikers));
    };
    bool Hard = false;
    float OverlapLeft = Overlap > 0.0f ? getOverlap(Slowed + Pushed) : 0.0f;
    const bool TooDeep = OverlapLeft > SoftOverlap || (Striking && OverlapLeft > 0.0f &&
                                                        getStrikerOverlap(Slowed + Pushed) > 0.0f);
    if (TooDeep) {
        Pushed = std::max(Pushed, Needed - Slowed);
        OverlapLeft = getOverlap(Slowed + Pushed);
        Hard = true;
    }
    const float Shift = Slowed + Pushed;
    const SpacingReport Report{.Standing = true,
                               .Overlap = std::max(Overlap, 0.0f),
                               .Needed = Needed,
                               .Slowed = Slowed,
                               .Pushed = Pushed,
                               .PushSpeed = Pushed / Dt,
                               .Hard = Hard,
                               .Eased = !Hard && Needed - Slowed > MaxPush + 1e-6f};
    if constexpr (FIGHTER_DEBUG) {
        if (Hard) {
            debug::logEvent(std::format("spacing hard push: bodies overlap {:.3f} m, apart {:.3f} m at once", Overlap,
                                        Pushed));
        } else if (LastPush < MinLoggedPush && Pushed / Dt >= MinLoggedPush) {
            debug::logEvent(std::format("spacing pushes apart: {}needs {:.3f} m more than the approach",
                                        Overlap > 0.0f ? std::format("bodies overlap {:.3f} m, ", Overlap) : "",
                                        Needed - Slowed));
        } else if (LastPush >= MinLoggedPush && Pushed / Dt < MinLoggedPush) {
            debug::logEvent("spacing push over");
        }
    }
    const auto [LeftX, RightX] = place(Shift);
    const auto [LeftSlowX, RightSlowX] = place(Slowed);
    // Without the walls: what the walls move over to the other fighter.
    const float LeftFree = LeftPlanned - Shift * LeftShare;
    const float RightFree = RightPlanned + Shift * (1.0f - LeftShare);
    for (auto&& [Motion, Planned, Corrected, SlowedTo, Free] :
         {std::tuple(&LeftMotion, LeftPlanned, LeftX, LeftSlowX, LeftFree),
          std::tuple(&RightMotion, RightPlanned, RightX, RightSlowX, RightFree)}) {
        // A hard push does not make the next push faster than an eased one.
        const PelvisController::SpacingMotion Split = splitCorrection(*Motion, Planned, Corrected, Dt);
        // A walk held back goes on at the speed it was let go (without the
        // knockback and the clip's travel): it picks up again gently.
        if (Split.Slowed != 0.0f) {
            Motion->slowWalk((Corrected - Motion->getPositionX() - Motion->getPlannedClipTravel()) / Dt -
                             Motion->getKnockback());
        }
        Motion->setSpacingMotion({.Slowed = Split.Slowed,
                                  .Pushed = Split.Pushed,
                                  .Eased = std::clamp((Corrected - SlowedTo) / Dt, -Params.PushMaxSpeed,
                                                      Params.PushMaxSpeed),
                                  .Wall = (Corrected - Free) / Dt,
                                  .Overlap = OverlapLeft});
    }
    if (Shift <= 0.0f) return Report;
    Left.pushBody(LeftX - LeftPlanned);
    Right.pushBody(RightX - RightPlanned);
    LeftMotion.limit(-MaxX, MaxX);
    RightMotion.limit(-MaxX, MaxX);
    return Report;
}

/// Keeps a standing pelvis away from the body of a fighter on the floor.
/// Returns how far it stepped off a body under it, m (signed along X).
float keepOffLying(Rig& Standing, const Rig& Lying, float MaxX, const SpacingParams& Params, float Dt) {
    const ExtentX Body = Lying.getExtentX();
    const float Clearance = Params.BodyHalfWidth + Standing.getControl().LyingClearance;
    const float Low = Body.Min - Clearance;
    const float High = Body.Max + Clearance;
    PelvisController& Motion = Standing.getController();
    const float Now = Motion.getPositionX();
    if (Now <= Low) {
        Motion.limit(-MaxX, Low);
        return 0.0f;
    }
    if (Now >= High) {
        Motion.limit(High, MaxX);
        return 0.0f;
    }
    // The body fell onto the fighter's feet: step out to the nearer side.
    const float Edge = Now - Low <= High - Now ? Low : High;
    const float Step = std::clamp(Edge - Now, -Params.SeparationSpeed * Dt, Params.SeparationSpeed * Dt);
    Motion.shift(Now + Step - Motion.getPlannedX());
    Motion.limit(-MaxX, MaxX);
    return Step;
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
    const float Wall = Motion.getWallShift() / Dt + Spacing.Wall;
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
