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

std::string_view getStanceVariantName(StanceVariant Variant) {
    switch (Variant) {
        case StanceVariant::Normal: return "normal";
        case StanceVariant::Switched: return "switched";
    }
    return "?";
}

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

LegCycle::LegCycle(float NewPeriodSec, std::vector<SupportSpan> NewSpans, size_t NewNormalSpan)
    : PeriodSec(NewPeriodSec), Spans(std::move(NewSpans)), NormalSpan(NewNormalSpan) {
    if (Spans.empty()) Spans.push_back({});
    NormalSpan = std::min(NormalSpan, Spans.size() - 1);
    settle();
}

void LegCycle::walk(float Dt, float Rate, float NewDirection) {
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
    const StopPlan Plan = planStop();
    const float Step = Dt * std::max(Rate, anim::MinPlaybackRate);
    if (std::abs(Plan.Offset) <= Step) {
        TimeSec = wrapTime(TimeSec + Plan.Offset, PeriodSec);
        CurrentMode = Mode::Still;
        return;
    }
    TimeSec = wrapTime(TimeSec + std::copysign(Step, Plan.Offset), PeriodSec);
    CurrentMode = Mode::Stopping;
}

void LegCycle::settle() {
    StepSec = 0.0f;
    TimeSec = getNormalTime();
    CurrentMode = Mode::Still;
    Engaged = false;
}

float LegCycle::getStopTarget() const { return wrapTime(TimeSec + planStop().Offset, PeriodSec); }

StanceVariant LegCycle::getVariant() const {
    return planStop().Span == NormalSpan ? StanceVariant::Normal : StanceVariant::Switched;
}

float LegCycle::getNormalTime() const { return getMiddle(Spans[NormalSpan], PeriodSec); }

LegCycle::StopPlan LegCycle::planStop() const {
    StopPlan Best{.Offset = std::numeric_limits<float>::max(), .Span = NormalSpan};
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

LegCycle makeLegCycle(const anim::Clip& Cycle, const anim::Pose& Base, const rig::Rig& Body) {
    const auto getPose = [&](float TimeSec) {
        anim::Pose Pose = Base;
        anim::layerPose(Pose, anim::sampleClip(Cycle, TimeSec));
        return Pose;
    };
    const auto getLift = [&](float TimeSec) {
        const anim::Pose Pose = getPose(TimeSec);
        return std::max(Body.getSoleHeight(Pose.Angles, BodyPart::FootL),
                        Body.getSoleHeight(Pose.Angles, BodyPart::FootR));
    };
    std::vector<SupportSpan> Spans =
        findSupportSpans(Cycle.DurationSec, getLift, Body.getControl().FootPlantHeight);

    // The normal stance: the support pose closest to the base pose.
    const anim::Pose& Mask = Cycle.Keys.front().Target;
    size_t Normal = 0;
    float NormalDistance = std::numeric_limits<float>::max();
    for (size_t Index = 0; Index < Spans.size(); ++Index) {
        const anim::Pose Pose = getPose(getMiddle(Spans[Index], Cycle.DurationSec));
        float Distance = 0.0f;
        for (size_t Part = 0; Part < BodyPartCount; ++Part) {
            if (!Mask.Mask.test(Part)) continue;
            const float Difference = Pose.Angles[Part] - Base.Angles[Part];
            Distance += Difference * Difference;
        }
        if (Distance < NormalDistance) {
            NormalDistance = Distance;
            Normal = Index;
        }
    }
    return LegCycle(Cycle.DurationSec, std::move(Spans), Normal);
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
