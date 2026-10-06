#include "combat/leg_step.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

#include "debug/draw.hpp"

namespace fighter::combat {
namespace {

constexpr std::array Feet = {BodyPart::FootL, BodyPart::FootR};
/// A foot whose sole is higher than this is in the air: it must step down, m.
constexpr float InAirHeight = 0.01f;
/// Points of the drawn arc.
constexpr int ArcSegments = 12;
/// Size of the cross at the landing target, m.
constexpr float TargetMarkSize = 0.08f;

float smoothStep(float T);
float lerp(float From, float To, float T);
rig::FootPlacement standOnFloor(rig::FootPlacement Placed);

} // namespace

LegStep LegStep::plan(const rig::LegStance& Now, const rig::LegStance& Target, bool ClipLeftLeg, bool ClipRightLeg,
                      bool Strike, float NewDurationSec, const LegStepTuning& Tuning) {
    LegStep Result;
    Result.Start = Now;
    for (const BodyPart Foot : Feet) Result.Start.getFoot(Foot) = standOnFloor(Now.getFoot(Foot));
    Result.Standing = Result.Start;
    Result.DurationSec = std::max(NewDurationSec, 0.0f);
    Result.LiftHeight = Tuning.LiftHeight;
    if (Result.DurationSec <= 0.0f) return Result;

    const auto isInAir = [&](BodyPart Foot) { return Now.getFoot(Foot).SoleHeight > InAirHeight; };
    const auto isOff = [&](BodyPart Foot) {
        return std::abs(Now.getFoot(Foot).Ankle.X - Target.getFoot(Foot).Ankle.X) > Tuning.MinDistance;
    };
    const auto isClipLeg = [&](BodyPart Foot) { return Foot == BodyPart::FootL ? ClipLeftLeg : ClipRightLeg; };

    // One foot stands while the other steps. First a supporting foot caught
    // in the air lands where the start pose has it, then a foot of the clip
    // caught in the air steps into the clip, then the feet off their
    // target. A strike leaves a supporting foot on the floor where it
    // stands (the reach comes from the hip, which does not move, and the
    // kicking leg lifts at once). A foot of the clip steps into the clip of
    // the moment, so if anything stepped before it, it catches up.
    std::vector<FootStep> Planned;
    const auto add = [&](BodyPart Foot) {
        if (std::ranges::any_of(Planned, [&](const FootStep& Step) { return Step.Foot == Foot; })) return;
        Planned.push_back({.Foot = Foot, .From = Result.Start.getFoot(Foot)});
        if (!isClipLeg(Foot)) Planned.back().To = Target.getFoot(Foot);
    };
    for (const BodyPart Foot : Feet) {
        if (isInAir(Foot) && !isClipLeg(Foot)) add(Foot);
    }
    for (const BodyPart Foot : Feet) {
        if (isInAir(Foot) && isClipLeg(Foot)) add(Foot);
    }
    for (const BodyPart Foot : Feet) {
        if (isOff(Foot) && (isClipLeg(Foot) || !Strike)) add(Foot);
    }
    if (Planned.empty()) return Result;
    for (const BodyPart Foot : Feet) {
        if (isClipLeg(Foot)) add(Foot);
    }
    const float Slot = Result.DurationSec / static_cast<float>(Planned.size());
    for (size_t Index = 0; Index < Planned.size() && Index < MaxFootSteps; ++Index) {
        Planned[Index].BeginSec = Slot * static_cast<float>(Index);
        Planned[Index].EndSec = Slot * static_cast<float>(Index + 1);
        Result.Steps[Index] = Planned[Index];
    }
    Result.Active = true;
    return Result;
}

void LegStep::advance(float Dt, const rig::LegStance& Now) {
    if (!Active) return;
    const float Before = ElapsedSec;
    ElapsedSec += Dt;
    if (ElapsedSec >= DurationSec) Active = false;
    for (const BodyPart Foot : Feet) {
        const rig::FootPlacement& Placed = Now.getFoot(Foot);
        Standing.getFoot(Foot).Planted = Placed.Planted;
        if (Placed.Planted) Standing.getFoot(Foot) = standOnFloor(Placed);
    }
    for (auto& Step : Steps) {
        if (!Step || Step->BeginSec <= 0.0f || Step->BeginSec <= Before || Step->BeginSec > ElapsedSec) continue;
        Step->From = Standing.getFoot(Step->Foot);
    }
}

rig::LegStance LegStep::getStance(const rig::LegStance& Target) const {
    rig::LegStance Result = Target;
    if (!Active) return Result;
    Result.PelvisHeight = lerp(Start.PelvisHeight, Target.PelvisHeight, smoothStep(ElapsedSec / DurationSec));
    for (const BodyPart Foot : Feet) Result.getFoot(Foot) = placeFoot(Foot, ElapsedSec, Target);
    return Result;
}

void LegStep::apply(PerBodyPart<float>& Angles, const rig::Rig& Body) const {
    if (!Active) return;
    const rig::LegStance Stance = getStance(Body.measureLegs(Angles));
    for (const BodyPart Foot : Feet) {
        const rig::FootPlacement& Placed = Stance.getFoot(Foot);
        Body.reachFoot(Angles, Foot, Stance.PelvisHeight, Placed.Ankle, Placed.Angle);
    }
}

std::optional<BodyPart> LegStep::getSteppingFoot() const {
    const FootStep* Current = findCurrentStep();
    return Current ? std::optional(Current->Foot) : std::nullopt;
}

float LegStep::getStepProgress() const {
    const FootStep* Current = findCurrentStep();
    return Current ? (ElapsedSec - Current->BeginSec) / (Current->EndSec - Current->BeginSec) : 0.0f;
}

std::string LegStep::describe() const {
    if (!Active) return "-";
    std::string Text;
    for (const auto& Step : Steps) {
        if (!Step) continue;
        if (!Text.empty()) Text += ", then ";
        const std::string_view Name = getBodyPartName(Step->Foot);
        if (ElapsedSec >= Step->EndSec) {
            Text += std::format("{} done", Name);
        } else if (ElapsedSec >= Step->BeginSec) {
            Text += std::format("step {} {:.2f}", Name, getStepProgress());
        } else {
            Text += std::string(Name);
        }
    }
    return Text;
}

void LegStep::drawDebug(const rig::LegStance& Target, const rig::Rig& Body) const {
    if constexpr (FIGHTER_DEBUG) {
        const FootStep* Current = findCurrentStep();
        if (!Current) return;
        const FootStep& Step = *Current;
        const BodyPart Foot = Step.Foot;
        const float PelvisX = Body.getPartPosition(BodyPart::Pelvis).X;
        const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
        const auto toWorld = [&](Vec2 Point) { return Vec2{PelvisX + Point.X * Facing, Point.Y}; };
        const rig::FootPlacement& To = Step.To.value_or(Target.getFoot(Foot));
        Vec2 Previous = toWorld(Step.From.Ankle);
        for (int Index = 1; Index <= ArcSegments; ++Index) {
            const float Share = static_cast<float>(Index) / static_cast<float>(ArcSegments);
            const Vec2 Point = toWorld(placeOnArc(Step, To, Share).Ankle);
            debug::drawLine(debug::Cat::TargetPose, Previous, Point);
            Previous = Point;
        }
        debug::drawCross(debug::Cat::TargetPose, toWorld(To.Ankle), TargetMarkSize);
        debug::drawText(debug::Cat::TargetPose, toWorld(To.Ankle),
                        std::format("step {} {:.0f}%", getBodyPartName(Foot), getStepProgress() * 100.0f));
    }
}

const FootStep* LegStep::findCurrentStep() const {
    if (!Active) return nullptr;
    for (const auto& Step : Steps) {
        if (Step && ElapsedSec >= Step->BeginSec && ElapsedSec < Step->EndSec) return &*Step;
    }
    return nullptr;
}

rig::FootPlacement LegStep::placeFoot(BodyPart Foot, float TimeSec, const rig::LegStance& Target) const {
    // On its step; else where the rig holds it planted (before its step, or
    // after it while another foot steps); a foot that landed and is not
    // planted yet stays where its step put it.
    const FootStep* Latest = nullptr;
    for (const auto& Step : Steps) {
        if (Step && Step->Foot == Foot && TimeSec >= Step->BeginSec) Latest = &*Step;
    }
    if (Latest && TimeSec < Latest->EndSec) {
        const rig::FootPlacement To = Latest->To.value_or(Target.getFoot(Foot));
        return placeOnArc(*Latest, To, (TimeSec - Latest->BeginSec) / (Latest->EndSec - Latest->BeginSec));
    }
    const rig::FootPlacement& Held = Standing.getFoot(Foot);
    if (Latest && !Held.Planted) return Latest->To.value_or(Target.getFoot(Foot));
    return Held;
}

rig::FootPlacement LegStep::placeOnArc(const FootStep& Step, const rig::FootPlacement& To, float Share) const {
    const float Clamped = std::clamp(Share, 0.0f, 1.0f);
    const float Along = smoothStep(Clamped);
    const float Lift = LiftHeight * std::sin(std::numbers::pi_v<float> * Clamped);
    return {.Ankle = {lerp(Step.From.Ankle.X, To.Ankle.X, Along), lerp(Step.From.Ankle.Y, To.Ankle.Y, Along) + Lift},
            .Angle = lerp(Step.From.Angle, To.Angle, Along),
            .SoleHeight = lerp(Step.From.SoleHeight, To.SoleHeight, Along) + Lift};
}

namespace {

/// 0 -> 0, 1 -> 1, flat at both ends.
float smoothStep(float T) {
    const float Clamped = std::clamp(T, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

float lerp(float From, float To, float T) { return From + (To - From) * T; }

/// A sole measured a little under the floor stands on it.
rig::FootPlacement standOnFloor(rig::FootPlacement Placed) {
    if (Placed.SoleHeight >= 0.0f) return Placed;
    Placed.Ankle.Y -= Placed.SoleHeight;
    Placed.SoleHeight = 0.0f;
    return Placed;
}

} // namespace

} // namespace fighter::combat
