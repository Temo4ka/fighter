#include "combat/leg_cycle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace fighter::combat {
namespace {

/// Offsets closer than this count as equal: the tie goes the way of the walk.
constexpr float TieSec = 1e-5f;

float wrapTime(float TimeSec, float PeriodSec);
float getWidth(const SupportSpan& Span, float PeriodSec);
bool isInside(const SupportSpan& Span, float TimeSec, float PeriodSec);
float getMiddle(const SupportSpan& Span, float PeriodSec);

} // namespace

std::vector<SupportSpan> findSupportSpans(float PeriodSec, const std::function<float(float)>& GetLift, float MaxLift,
                                          float SampleSec) {
    const auto Count = static_cast<size_t>(std::max(1.0f, std::ceil(PeriodSec / SampleSec)));
    const float Step = PeriodSec / static_cast<float>(Count);
    std::vector<bool> Down(Count);
    size_t Lowest = 0;
    float LowestLift = std::numeric_limits<float>::max();
    for (size_t Index = 0; Index < Count; ++Index) {
        const float Lift = GetLift(static_cast<float>(Index) * Step);
        Down[Index] = Lift <= MaxLift;
        if (Lift < LowestLift) {
            LowestLift = Lift;
            Lowest = Index;
        }
    }
    const auto DownCount = std::ranges::count(Down, true);
    if (DownCount == 0) {
        const float At = static_cast<float>(Lowest) * Step;
        return {SupportSpan{.BeginSec = At, .EndSec = At}};
    }
    if (static_cast<size_t>(DownCount) == Count) return {SupportSpan{.BeginSec = 0.0f, .EndSec = PeriodSec}};

    // Start right after a sample in the air, so that no run is cut in two.
    size_t Start = 0;
    while (Down[Start]) ++Start;
    std::vector<SupportSpan> Spans;
    std::optional<size_t> RunBegin;
    for (size_t Visited = 1; Visited <= Count; ++Visited) {
        const size_t Index = (Start + Visited) % Count;
        if (Down[Index] && !RunBegin) RunBegin = Index;
        const bool RunEnds = Down[Index] && !Down[(Index + 1) % Count];
        if (RunEnds) {
            Spans.push_back({.BeginSec = static_cast<float>(*RunBegin) * Step,
                             .EndSec = static_cast<float>(Index) * Step});
            RunBegin.reset();
        }
    }
    std::ranges::sort(Spans, {}, &SupportSpan::BeginSec);
    return Spans;
}

LegCycle::LegCycle(float NewPeriodSec, std::vector<SupportSpan> NewSpans)
    : PeriodSec(NewPeriodSec), Spans(std::move(NewSpans)) {
    if (Spans.empty()) Spans.push_back({});
    settle();
}

void LegCycle::walk(float Dt, float Rate, float NewDirection) {
    Chosen.reset();
    Direction = NewDirection > 0.0f ? 1.0f : -1.0f;
    StepSec = Dt * Rate * Direction;
    TimeSec = wrapTime(TimeSec + StepSec, PeriodSec);
    CurrentMode = Mode::Walking;
    Engaged = true;
}

void LegCycle::follow(float Share) {
    const float Kept = StepSec * std::clamp(Share, 0.0f, 1.0f);
    TimeSec = wrapTime(TimeSec - StepSec + Kept, PeriodSec);
    StepSec = Kept;
}

float LegCycle::getStepFromTime() const { return wrapTime(TimeSec - StepSec, PeriodSec); }

void LegCycle::stop(float Dt, float Rate) {
    StepSec = 0.0f;
    if (CurrentMode == Mode::Still) return;
    playStop(planStop(), Dt, Rate);
}

void LegCycle::hold() {
    StepSec = 0.0f;
    Chosen.reset();
    if (CurrentMode == Mode::Still) return;
    CurrentMode = planStop().Offset == 0.0f ? Mode::Still : Mode::Held;
}

void LegCycle::playStop(const StopPlan& Plan, float Dt, float Rate) {
    const float Step = Dt * std::max(Rate, anim::MinPlaybackRate);
    if (std::abs(Plan.Offset) <= Step) {
        TimeSec = wrapTime(TimeSec + Plan.Offset, PeriodSec);
        CurrentMode = Mode::Still;
        return;
    }
    TimeSec = wrapTime(TimeSec + std::copysign(Step, Plan.Offset), PeriodSec);
    CurrentMode = Mode::Stopping;
}

void LegCycle::settle(BodyPart FrontFoot) {
    StepSec = 0.0f;
    Chosen.reset();
    TimeSec = getRestTime(FrontFoot);
    CurrentMode = Mode::Still;
    Engaged = false;
}

float LegCycle::getStopTarget() const { return wrapTime(TimeSec + planStop().Offset, PeriodSec); }

BodyPart LegCycle::getFrontFoot() const { return Spans[planStop().Span].FrontFoot; }

float LegCycle::getRestTime(BodyPart FrontFoot) const {
    const auto Found = std::ranges::find(Spans, FrontFoot, &SupportSpan::FrontFoot);
    return getMiddle(Found != Spans.end() ? *Found : Spans.front(), PeriodSec);
}

std::vector<StopTarget> LegCycle::getStopTargets() const {
    std::vector<StopTarget> Targets;
    for (size_t Index = 0; Index < Spans.size(); ++Index) {
        const SupportSpan& Span = Spans[Index];
        if (isInside(Span, TimeSec, PeriodSec)) return {};
        // Aim a little inside the span, so that rounding cannot stop the
        // phase just outside it.
        const float Margin = getWidth(Span, PeriodSec) * 0.25f;
        const float Forward = wrapTime(Span.BeginSec + Margin - TimeSec, PeriodSec);
        const float Backward = -wrapTime(TimeSec - (Span.EndSec - Margin), PeriodSec);
        // The nearer way round.
        const float Offset = Forward <= -Backward ? Forward : Backward;
        Targets.push_back({.TimeSec = wrapTime(TimeSec + Offset, PeriodSec), .Offset = Offset, .Span = Index});
    }
    return Targets;
}

void LegCycle::chooseStop(const StopTarget& Target) { Chosen = Target; }

LegCycle::StopPlan LegCycle::planStop() const {
    if (Chosen) {
        // The rest of the way to the chosen target, the same way round.
        const float Left = Chosen->Offset > 0.0f ? wrapTime(Chosen->TimeSec - TimeSec, PeriodSec)
                                                 : -wrapTime(TimeSec - Chosen->TimeSec, PeriodSec);
        const bool Passed = std::abs(Left) > std::abs(Chosen->Offset) + TieSec;
        return {.Offset = Passed ? 0.0f : Left, .Span = Chosen->Span};
    }
    StopPlan Best{.Offset = std::numeric_limits<float>::max(), .Span = 0};
    for (size_t Index = 0; Index < Spans.size(); ++Index) {
        const SupportSpan& Span = Spans[Index];
        if (isInside(Span, TimeSec, PeriodSec)) return {.Offset = 0.0f, .Span = Index};
        // Aim a little inside the span, so that rounding cannot stop the
        // phase just outside it.
        const float Margin = getWidth(Span, PeriodSec) * 0.25f;
        const float Forward = wrapTime(Span.BeginSec + Margin - TimeSec, PeriodSec);
        const float Backward = -wrapTime(TimeSec - (Span.EndSec - Margin), PeriodSec);
        for (const float Offset : {Forward, Backward}) {
            const float Gain = std::abs(Best.Offset) - std::abs(Offset);
            const bool Tie = std::abs(Gain) <= TieSec;
            if (Gain > TieSec || (Tie && Offset * Direction > 0.0f)) Best = {.Offset = Offset, .Span = Index};
        }
    }
    return Best;
}

LegCycle makeLegCycle(const anim::Clip& Cycle, const anim::Pose& Base, const rig::Rig& Body, float MinSpread) {
    const auto measure = [&](float TimeSec) {
        anim::Pose Pose = Base;
        anim::layerPose(Pose, anim::sampleClip(Cycle, TimeSec));
        return Body.measureLegs(Pose.Angles);
    };
    // Feet too close together (passing each other) count as a foot in the
    // air: the legs do not rest there.
    const float NotWide = std::numeric_limits<float>::max();
    const auto getLift = [&](float TimeSec) {
        const rig::LegStance Legs = measure(TimeSec);
        if (Legs.getSpread() < MinSpread) return NotWide;
        return std::max(Legs.Left.SoleHeight, Legs.Right.SoleHeight);
    };
    std::vector<SupportSpan> Spans = findSupportSpans(Cycle.DurationSec, getLift, Body.getControl().FootPlantHeight);
    for (SupportSpan& Span : Spans) Span.FrontFoot = measure(getMiddle(Span, Cycle.DurationSec)).getFrontFoot();
    return LegCycle(Cycle.DurationSec, std::move(Spans));
}

namespace {

float wrapTime(float TimeSec, float PeriodSec) {
    const float Wrapped = std::fmod(TimeSec, PeriodSec);
    return Wrapped < 0.0f ? Wrapped + PeriodSec : Wrapped;
}

float getWidth(const SupportSpan& Span, float PeriodSec) {
    if (Span.EndSec - Span.BeginSec >= PeriodSec) return PeriodSec;
    return wrapTime(Span.EndSec - Span.BeginSec, PeriodSec);
}

bool isInside(const SupportSpan& Span, float TimeSec, float PeriodSec) {
    if (getWidth(Span, PeriodSec) >= PeriodSec) return true;
    if (Span.BeginSec <= Span.EndSec) return TimeSec >= Span.BeginSec && TimeSec <= Span.EndSec;
    return TimeSec >= Span.BeginSec || TimeSec <= Span.EndSec;
}

float getMiddle(const SupportSpan& Span, float PeriodSec) {
    return wrapTime(Span.BeginSec + getWidth(Span, PeriodSec) * 0.5f, PeriodSec);
}

} // namespace

} // namespace fighter::combat
