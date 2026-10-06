#include "combat/leg_cycle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace fighter::combat {
namespace {

/// Offsets closer than this count as equal: the tie goes the way of the walk.
constexpr float TieSec = 1e-5f;
/// The swing foot's share of a step never sets even time alone: the clip's
/// time keeps this share, so that even time keeps increasing.
constexpr float MaxEvenness = 0.98f;
/// A step whose swing foot moves less than this in the world is timed by
/// the clip, m.
constexpr float MinSwingGap = 0.01f;

float wrapTime(float TimeSec, float PeriodSec);
std::vector<float> makeEvenTable(float PeriodSec, std::vector<float> Middles,
                                 const std::function<rig::LegStance(float)>& Measure, float Evenness);
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

void LegCycle::setEvenTable(std::vector<float> Table) {
    EvenTable = Table.size() >= 2 ? std::move(Table) : std::vector<float>{};
}

float LegCycle::toEven(float ClipSec) const {
    if (EvenTable.empty()) return wrapTime(ClipSec, PeriodSec);
    const float Sample = PeriodSec / static_cast<float>(EvenTable.size() - 1);
    const float At = wrapTime(ClipSec, PeriodSec) / Sample;
    const auto Index = std::min(static_cast<size_t>(At), EvenTable.size() - 2);
    const float T = At - static_cast<float>(Index);
    return wrapTime(EvenTable[Index] + (EvenTable[Index + 1] - EvenTable[Index]) * T, PeriodSec);
}

float LegCycle::toClip(float EvenSec) const {
    if (EvenTable.empty()) return wrapTime(EvenSec, PeriodSec);
    const float Sample = PeriodSec / static_cast<float>(EvenTable.size() - 1);
    const float First = EvenTable.front();
    const float Even = First + wrapTime(EvenSec - First, PeriodSec);
    const auto Next = std::upper_bound(EvenTable.begin(), EvenTable.end(), Even);
    if (Next == EvenTable.begin()) return 0.0f;
    if (Next == EvenTable.end()) return wrapTime(PeriodSec, PeriodSec);
    const auto Index = static_cast<size_t>(std::prev(Next) - EvenTable.begin());
    const float Span = EvenTable[Index + 1] - EvenTable[Index];
    const float T = Span > 0.0f ? (Even - EvenTable[Index]) / Span : 0.0f;
    return wrapTime((static_cast<float>(Index) + T) * Sample, PeriodSec);
}

void LegCycle::walk(float Dt, float Rate, float NewDirection) {
    Chosen.reset();
    Direction = NewDirection > 0.0f ? 1.0f : -1.0f;
    StepSec = Dt * Rate * Direction;
    TimeSec = toClip(toEven(TimeSec) + StepSec);
    CurrentMode = Mode::Walking;
    Engaged = true;
}

void LegCycle::follow(float Share) {
    const float Kept = StepSec * std::clamp(Share, 0.0f, 1.0f);
    const bool Arrived = CurrentMode == Mode::Still && StepSec != 0.0f;
    TimeSec = toClip(toEven(TimeSec) - StepSec + Kept);
    StepSec = Kept;
    // A coast that reached its span but was held back short of it goes on.
    if (Arrived && Kept != StepSec) CurrentMode = Mode::Stopping;
}

float LegCycle::getStepFromTime() const { return toClip(toEven(TimeSec) - StepSec); }

void LegCycle::stop(float Dt, float Rate) {
    StepSec = 0.0f;
    if (CurrentMode == Mode::Still) return;
    playStop(planStop(), Dt, Rate);
}

void LegCycle::beginStop() {
    StepSec = 0.0f;
    Chosen.reset();
    if (isInSpan()) {
        CurrentMode = Mode::Still;
        return;
    }
    Chosen = findSpanAhead();
    CurrentMode = Mode::Stopping;
}

void LegCycle::coast(float Dt, float Rate, float NewDirection) {
    if (CurrentMode != Mode::Stopping) return;
    const float Offset = planStop().Offset;
    const float Left = getEvenOffset(Offset);
    const float Step = Dt * Rate * (NewDirection > 0.0f ? 1.0f : -1.0f);
    // Only towards the span; there it rests.
    if (Step * Left <= 0.0f && Left != 0.0f) {
        StepSec = 0.0f;
        return;
    }
    if (std::abs(Step) >= std::abs(Left)) {
        StepSec = Left;
        TimeSec = wrapTime(TimeSec + Offset, PeriodSec);
        CurrentMode = Mode::Still;
        return;
    }
    StepSec = Step;
    TimeSec = toClip(toEven(TimeSec) + Step);
}

void LegCycle::rest() {
    StepSec = 0.0f;
    Chosen.reset();
    CurrentMode = Mode::Still;
}

bool LegCycle::isInSpan() const {
    return std::ranges::any_of(Spans, [&](const SupportSpan& Span) { return isInside(Span, TimeSec, PeriodSec); });
}

float LegCycle::getLeftToSpanAhead() const {
    return isInSpan() ? 0.0f : std::abs(getEvenOffset(findSpanAhead().Offset));
}

float LegCycle::getEvenOffset(float Offset) const {
    if (Offset == 0.0f) return 0.0f;
    const float From = toEven(TimeSec);
    const float To = toEven(TimeSec + Offset);
    return Offset > 0.0f ? wrapTime(To - From, PeriodSec) : -wrapTime(From - To, PeriodSec);
}

StopTarget LegCycle::findSpanAhead() const {
    std::optional<StopTarget> Best;
    for (size_t Index = 0; Index < Spans.size(); ++Index) {
        const SupportSpan& Span = Spans[Index];
        // A little inside the span, so that rounding cannot stop the phase
        // just outside it.
        const float Margin = getWidth(Span, PeriodSec) * 0.25f;
        const float Offset = Direction > 0.0f ? wrapTime(Span.BeginSec + Margin - TimeSec, PeriodSec)
                                              : -wrapTime(TimeSec - (Span.EndSec - Margin), PeriodSec);
        if (!Best || std::abs(Offset) < std::abs(Best->Offset)) {
            Best = StopTarget{.TimeSec = wrapTime(TimeSec + Offset, PeriodSec), .Offset = Offset, .Span = Index};
        }
    }
    return Best.value_or(StopTarget{.TimeSec = TimeSec});
}

float LegCycle::getStopLeft() const {
    return CurrentMode == Mode::Stopping ? std::abs(getEvenOffset(planStop().Offset)) : 0.0f;
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

LegCycle makeLegCycle(const anim::Clip& Cycle, const anim::Pose& Base, const rig::Rig& Body, float MinSpread,
                      float Evenness) {
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
    std::vector<float> Middles;
    for (const SupportSpan& Span : Spans) Middles.push_back(getMiddle(Span, Cycle.DurationSec));
    LegCycle Result(Cycle.DurationSec, std::move(Spans));
    if (Evenness > 0.0f) Result.setEvenTable(makeEvenTable(Cycle.DurationSec, Middles, measure, Evenness));
    return Result;
}

namespace {

/// The even times of the clip times 0, P/N, ... P (LegCycle::setEvenTable):
/// in each step, from one span middle to the next, even time is the share
/// of the step the swing foot has gone in the world, blended by \p Evenness
/// with the share of the step's clip time.
std::vector<float> makeEvenTable(float PeriodSec, std::vector<float> Middles,
                                 const std::function<rig::LegStance(float)>& Measure, float Evenness) {
    const auto Count = static_cast<size_t>(std::max(1.0f, std::ceil(PeriodSec / SupportSampleSec)));
    const float Sample = PeriodSec / static_cast<float>(Count);
    const float Weight = std::clamp(Evenness, 0.0f, MaxEvenness);
    std::ranges::sort(Middles);
    std::vector<float> Table(Count + 1);
    for (size_t Step = 0; Step < Middles.size(); ++Step) {
        const float Begin = Middles[Step];
        const float End = Step + 1 < Middles.size() ? Middles[Step + 1] : Middles.front() + PeriodSec;
        const float Length = End - Begin;
        if (Length <= 0.0f) continue;
        // The clip samples of the step (clip time unwrapped from Begin).
        std::vector<float> Times;
        for (auto Index = static_cast<size_t>(std::ceil(Begin / Sample)); static_cast<float>(Index) * Sample <= End;
             ++Index) {
            Times.push_back(static_cast<float>(Index) * Sample);
        }
        std::vector<rig::LegStance> Legs;
        for (const float Time : Times) Legs.push_back(Measure(wrapTime(Time, PeriodSec)));
        // The swing foot: the one that is higher somewhere in the step.
        float LeftLift = 0.0f;
        float RightLift = 0.0f;
        for (const rig::LegStance& Each : Legs) {
            LeftLift = std::max(LeftLift, Each.Left.SoleHeight);
            RightLift = std::max(RightLift, Each.Right.SoleHeight);
        }
        const BodyPart Swing = LeftLift >= RightLift ? BodyPart::FootL : BodyPart::FootR;
        const BodyPart Other = Swing == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
        // The swing foot in the world: relative to the pelvis, plus the
        // travel the step makes, which the planted foot shows (it goes back
        // relative to the pelvis as far as the pelvis goes on).
        const rig::LegStance First = Measure(wrapTime(Begin, PeriodSec));
        const rig::LegStance Last = Measure(wrapTime(End, PeriodSec));
        const float Speed = (First.getFoot(Other).Ankle.X - Last.getFoot(Other).Ankle.X) / Length;
        const auto getWorldX = [&](const rig::LegStance& Each, float Time) {
            return Each.getFoot(Swing).Ankle.X + Speed * (Time - Begin);
        };
        const float From = getWorldX(First, Begin);
        const float To = getWorldX(Last, End);
        float Reached = 0.0f;
        for (auto&& [Time, Each] : std::views::zip(Times, Legs)) {
            const float ByTime = (Time - Begin) / Length;
            float ByFoot = ByTime;
            if (std::abs(To - From) > MinSwingGap) {
                Reached = std::max(Reached, std::clamp((getWorldX(Each, Time) - From) / (To - From), 0.0f, 1.0f));
                ByFoot = Reached;
            }
            const float Even = Begin + Length * (ByTime + (ByFoot - ByTime) * Weight);
            // Into [0, P]: a sample of the wrapping step before 0 is one period on.
            const auto Index = static_cast<size_t>(std::lround(Time / Sample)) % Count;
            Table[Index] = Time >= PeriodSec ? Even - PeriodSec : Even;
        }
    }
    // The table runs on through the period: the last sample is the first
    // one period on.
    Table[Count] = Table[0] + PeriodSec;
    for (size_t Index = 1; Index <= Count; ++Index) {
        if (Table[Index] < Table[Index - 1]) Table[Index] += PeriodSec;
    }
    return Table;
}

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
