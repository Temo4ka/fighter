//===- combat/leg_cycle.hpp - Leg cycles and support phases -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares LegCycle, the phase of a looping leg clip (the walk,
/// the crouch walk) and how it stops, and the search for the phases where
/// both feet stand on the floor wide apart (support spans).
///
/// A walk cycle has two wide double-support phases: one with the left foot
/// in front and one with the right foot in front (where the feet pass each
/// other they are both down too, but too close together to rest on). When
/// the move key is released the cycle does not play on to its start; it
/// plays on (or back) to the nearest such phase, quickly if a foot is in the
/// air, and holds it: that leg pose is the rest pose of the legs until an
/// action needs them, and walking again continues from that phase. The
/// spans are found from the clip and the body (the sole heights and the
/// ankles the rig computes), not written into the clip.
///
/// The cycle is cut into steps, from the middle of one span to the middle
/// of the next (CycleStep): one foot swings, the other stands. The fighter
/// places the feet of a step along the floor itself (Fighter: the standing
/// foot stays, the swing foot goes with the pelvis travel to where the
/// clip lands it); the clip gives the rest of the leg pose.
///
/// Played on quickly, the cycle also moves the foot that stands on the
/// floor relative to the pelvis: the rig holds it where it stands (a planted
/// foot, the leg bends to it) and the fighter keeps the feet there from then
/// on (rig::Rig::keepFeetPlanted), so nothing slides.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "rig/rig.hpp"

namespace fighter::combat {

/// A part of a cycle where both feet stand on the floor, s of clip time in
/// [0, period). BeginSec > EndSec: the span wraps through 0.
struct SupportSpan {
    float BeginSec = 0.0f;
    float EndSec = 0.0f;
    /// The foot in front in the middle of the span: FootL or FootR.
    BodyPart FrontFoot = BodyPart::FootL;
};

/// A phase a stop may head to: just inside a span, reached forwards
/// (Offset > 0) or backwards from the current phase.
struct StopTarget {
    float TimeSec = 0.0f;
    float Offset = 0.0f;   ///< Signed clip time from the current phase, s.
    size_t Span = 0;
};

/// One step of a cycle: from the middle of a span to the middle of the next.
struct CycleStep {
    float BeginSec = 0.0f;
    float EndSec = 0.0f;                 ///< Unwrapped: more than BeginSec.
    BodyPart Swing = BodyPart::FootR;    ///< The foot that is in the air in it.
    /// Where the clip has the swing foot at the begin and the end, as
    /// rig::FootPlacement::Ankle.X (relative to the pelvis, facing right), m.
    float SwingBeginX = 0.0f;
    float SwingEndX = 0.0f;
    /// The share of the step (0..1) where the clip lifts the swing foot
    /// above the plant height and where it sets it down again.
    float LiftShare = 0.0f;
    float LandShare = 1.0f;
};

/// Sampling step of the support span search, s of clip time.
inline constexpr float SupportSampleSec = 0.005f;

/// The spans of a cycle of \p PeriodSec where \p GetLift (clip time -> the
/// higher of the two soles above the floor, m) is at most \p MaxLift,
/// sampled every \p SampleSec. A cycle that never has both feet down gets a
/// single zero-length span at its lowest sample, so that it can still stop.
std::vector<SupportSpan> findSupportSpans(float PeriodSec, const std::function<float(float)>& GetLift, float MaxLift,
                                          float SampleSec = SupportSampleSec);

/// The phase of a looping leg clip: walking, stopping or held.
class LegCycle {
public:
    enum class Mode : uint8_t {
        Still,      ///< Holds its phase.
        Walking,    ///< Follows the walking speed.
        Stopping,   ///< Plays on (stop()) to a support span.
        Held,       ///< Keeps a mid-step pose: the opponent is in the way (hold()).
    };

    LegCycle() = default;
    /// A cycle of \p PeriodSec with its support spans. It starts settled
    /// with the left foot in front (settle()).
    LegCycle(float PeriodSec, std::vector<SupportSpan> Spans);
    /// The steps of the cycle (makeLegCycle() finds them), in clip order.
    void setSteps(std::vector<CycleStep> NewSteps) { Steps = std::move(NewSteps); }
    const std::vector<CycleStep>& getSteps() const { return Steps; }
    /// The step \p TimeSec (clip time) is in, if the cycle has steps.
    std::optional<size_t> findStep(float TimeSec) const;
    /// How far into step \p Step the clip time \p TimeSec is: 0 at its
    /// begin, 1 at its end (unclamped; the nearer way round).
    float getStepShare(size_t Step, float TimeSec) const;

    /// Walks for \p Dt: the phase runs \p Rate clip seconds per second,
    /// forwards for \p Direction > 0, backwards (a step back) otherwise.
    void walk(float Dt, float Rate, float Direction);
    /// After walk(): keeps only \p Share (0..1) of the step it made, when
    /// the pelvis travelled only that share of what the step assumed
    /// (rig::Rig::getTravelShare).
    void follow(float Share);
    /// The phase before the last walk() (the current one if the cycle did
    /// not walk since), s.
    float getStepFromTime() const;
    /// No input for \p Dt: a walking cycle stops. It holds where it is if
    /// both feet are down, otherwise it plays on to the target chooseStop()
    /// set, or to the nearest point of a support span (either way round),
    /// at \p Rate clip seconds per second.
    void stop(float Dt, float Rate);
    /// Where a stop could head from the current phase: into each span, the
    /// nearer way round. None when the phase is in a span.
    std::vector<StopTarget> getStopTargets() const;
    /// The stop heads to \p Target (one of getStopTargets()) until the
    /// cycle walks again, holds or settles.
    void chooseStop(const StopTarget& Target);
    /// Rests where it is, also in mid-step (the walk was released: the
    /// fighter sets the swing foot down and settles into the stance).
    void rest();
    /// Is the phase inside a support span (both feet down, wide)?
    bool isInSpan() const;
    /// Clip time left to the span a stop heads to (0 when none), s.
    float getStopLeft() const;
    /// Clip time from the phase to the next span in the direction of the
    /// last walk (0 inside a span), s: what is left of the step going on.
    float getLeftToSpanAhead() const;
    /// A walk the opponent holds back stops where it is: with both feet down
    /// as stop() does at once; in mid-step it keeps its pose (Mode::Held)
    /// until it walks again or settle(), because playing the step on (or
    /// back) without travel would set a foot down on the opponent's.
    void hold();
    /// Holds its walk pose in mid-step (hold()).
    bool isHeld() const { return CurrentMode == Mode::Held; }
    /// Jumps to the middle of the first span with \p FrontFoot in front (the
    /// first span if none) and holds it; the cycle is not engaged any more
    /// (another clip took the legs over and left them in a stance with that
    /// foot in front).
    void settle(BodyPart FrontFoot = BodyPart::FootL);

    Mode getMode() const { return CurrentMode; }
    /// Walking or stopping: the clip must be shown (also while held, isHeld()).
    bool isPlaying() const { return CurrentMode == Mode::Walking || CurrentMode == Mode::Stopping; }
    bool isStopping() const { return CurrentMode == Mode::Stopping; }
    /// Has it played since the last settle()? Its held pose is then the one
    /// the legs stand in.
    bool isEngaged() const { return Engaged; }
    float getTime() const { return TimeSec; }
    /// The direction of the last walk: +1 forwards through the clip, -1 back.
    float getDirection() const { return Direction; }
    float getPeriod() const { return PeriodSec; }
    /// The phase a stop heads to (the current one when held).
    float getStopTarget() const;
    /// The front foot of the span the phase is in or a stop heads to.
    BodyPart getFrontFoot() const;
    /// The middle of the first span with \p FrontFoot in front, s.
    float getRestTime(BodyPart FrontFoot) const;
    const std::vector<SupportSpan>& getSpans() const { return Spans; }

private:
    /// The signed clip time from the phase to where a stop would hold, and
    /// the span it lands in.
    struct StopPlan {
        float Offset = 0.0f;
        size_t Span = 0;
    };
    StopPlan planStop() const;
    /// The nearest span ahead in the direction of the last walk.
    StopTarget findSpanAhead() const;
    void playStop(const StopPlan& Plan, float Dt, float Rate);

    float PeriodSec = 1.0f;
    std::vector<SupportSpan> Spans{SupportSpan{}};
    std::vector<CycleStep> Steps;   ///< setSteps().
    float TimeSec = 0.0f;
    float StepSec = 0.0f;     ///< How far the last walk() moved the phase (signed); 0 after stop(), settle().
    float Direction = 1.0f;   ///< Of the last walk: a tie is broken that way.
    std::optional<StopTarget> Chosen;   ///< chooseStop(); its Offset is from the phase then.
    Mode CurrentMode = Mode::Still;
    bool Engaged = false;
};

/// The leg cycle of the looping clip \p Cycle played over \p Base (the
/// stance or the crouch): the support spans are where both soles of
/// \p Body are below the rig's foot plant height (rig::Rig::measureLegs)
/// and the ankles at least \p MinSpread apart, m. Its steps go from span
/// middle to span middle.
LegCycle makeLegCycle(const anim::Clip& Cycle, const anim::Pose& Base, const rig::Rig& Body, float MinSpread);

} // namespace fighter::combat
