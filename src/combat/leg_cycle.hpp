//===- combat/leg_cycle.hpp - Leg cycles and support phases -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares LegCycle, the phase of a looping leg clip (the walk,
/// the crouch walk) and how it stops, and the search for the phases where
/// both feet stand on the floor (support spans).
///
/// A walk cycle has two double-support phases: one with the left (lead) foot
/// in front, as in the stance, and one with the right foot in front. When
/// the move key is released the cycle does not play on to its start; it
/// stops at the nearest support phase, played there quickly if a foot is in
/// the air, and holds it. The fighter then stands in the normal stance or in
/// the "switched" one (right foot forward), and walking again continues
/// from that phase. The spans are found from the clip and the body (the
/// sole heights the rig computes), not written into the clip.
///
/// Played on quickly, the cycle also moves the foot that stands on the
/// floor: the fighter lets it slide meanwhile (rig::Rig::slideFeet), so that
/// the stop ends in the clip's support pose with both feet down, and keeps
/// the feet where they are from then on (rig::Rig::keepFeetPlanted).
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "rig/rig.hpp"

namespace fighter::combat {

/// Which foot is in front while the fighter stands still.
enum class StanceVariant : uint8_t {
    Normal,     ///< The left (lead) foot forward, as in the stance clip.
    Switched,   ///< The right foot forward: the other support phase.
};

std::string_view getStanceVariantName(StanceVariant Variant);

/// A part of a cycle where both feet stand on the floor, s of clip time in
/// [0, period). BeginSec > EndSec: the span wraps through 0.
struct SupportSpan {
    float BeginSec = 0.0f;
    float EndSec = 0.0f;
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
        Stopping,   ///< Plays on to the nearest support span, then holds.
    };

    LegCycle() = default;
    /// A cycle of \p PeriodSec with its support spans; \p NormalSpan indexes
    /// \p Spans: the normal stance. It starts held in the normal stance.
    LegCycle(float PeriodSec, std::vector<SupportSpan> Spans, size_t NormalSpan);

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
    /// both feet are down, otherwise it plays on to the nearest point of a
    /// support span (either way round) at \p Rate clip seconds per second.
    void stop(float Dt, float Rate);
    /// Jumps to the middle of the normal span and holds it; the cycle is
    /// not engaged any more (another clip took the legs over).
    void settle();

    Mode getMode() const { return CurrentMode; }
    /// Walking or stopping: the clip must be shown.
    bool isPlaying() const { return CurrentMode != Mode::Still; }
    bool isStopping() const { return CurrentMode == Mode::Stopping; }
    /// Has it played since the last settle()? Its held pose is then the one
    /// the legs stand in.
    bool isEngaged() const { return Engaged; }
    float getTime() const { return TimeSec; }
    float getPeriod() const { return PeriodSec; }
    /// The phase a stop heads to (the current one when held).
    float getStopTarget() const;
    /// The stance of the span the phase is in or nearest to.
    StanceVariant getVariant() const;
    /// The middle of the normal span, s.
    float getNormalTime() const;
    const std::vector<SupportSpan>& getSpans() const { return Spans; }

private:
    /// The signed clip time from the phase to where a stop would hold, and
    /// the span it lands in.
    struct StopPlan {
        float Offset = 0.0f;
        size_t Span = 0;
    };
    StopPlan planStop() const;

    float PeriodSec = 1.0f;
    std::vector<SupportSpan> Spans{SupportSpan{}};
    size_t NormalSpan = 0;
    float TimeSec = 0.0f;
    float StepSec = 0.0f;     ///< How far the last walk() moved the phase (signed); 0 after stop(), settle().
    float Direction = 1.0f;   ///< Of the last walk: a tie is broken that way.
    Mode CurrentMode = Mode::Still;
    bool Engaged = false;
};

/// The leg cycle of the looping clip \p Cycle played over \p Base (the
/// stance or the crouch): the support spans come from the sole heights of
/// \p Body (rig::Rig::getSoleHeight, below the rig's foot plant height);
/// the normal stance is the span whose pose is closest to \p Base.
LegCycle makeLegCycle(const anim::Clip& Cycle, const anim::Pose& Base, const rig::Rig& Body);

} // namespace fighter::combat
