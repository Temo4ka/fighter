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

} // namespace

LegStep LegStep::plan(const rig::LegStance& Now, const rig::LegStance& Target, bool ClipLeftLeg, bool ClipRightLeg,
                      float NewDurationSec, const LegStepTuning& Tuning) {
    LegStep Result;
    Result.Start = Now;
    Result.DurationSec = std::max(NewDurationSec, 0.0f);
    Result.LiftHeight = Tuning.LiftHeight;

    // The feet that must move: off their target, or in the air.
    const auto mustMove = [&](BodyPart Foot) {
        const rig::FootPlacement& From = Now.getFoot(Foot);
        return std::abs(From.Ankle.X - Target.getFoot(Foot).Ankle.X) > Tuning.MinDistance ||
               From.SoleHeight > InAirHeight;
    };
    const auto isClipLeg = [&](BodyPart Foot) { return Foot == BodyPart::FootL ? ClipLeftLeg : ClipRightLeg; };
    std::vector<BodyPart> Moving;
    for (const BodyPart Foot : Feet) {
        if (mustMove(Foot)) Moving.push_back(Foot);
    }
    if (Moving.empty() || Result.DurationSec <= 0.0f) return Result;
    // A leg the clip moves would lift while the other foot steps: it steps
    // too, last, into the clip's pose of that moment.
    for (const BodyPart Foot : Feet) {
        if (isClipLeg(Foot) && std::ranges::find(Moving, Foot) == Moving.end()) Moving.push_back(Foot);
    }
    // The supporting foot first, then the one the clip moves; else the
    // farther one first.
    std::ranges::stable_sort(Moving, [&](BodyPart First, BodyPart Second) {
        if (isClipLeg(First) != isClipLeg(Second)) return !isClipLeg(First);
        const auto getDistance = [&](BodyPart Foot) {
            return std::abs(Now.getFoot(Foot).Ankle.X - Target.getFoot(Foot).Ankle.X);
        };
        return getDistance(First) > getDistance(Second);
    });
    const float Slot = Result.DurationSec / static_cast<float>(Moving.size());
    for (size_t Index = 0; Index < Moving.size(); ++Index) {
        Result.Steps[Index] = FootStep{.Foot = Moving[Index],
                                       .BeginSec = Slot * static_cast<float>(Index),
                                       .EndSec = Slot * static_cast<float>(Index + 1),
                                       .From = Now.getFoot(Moving[Index])};
    }
    Result.Active = true;
    return Result;
}

void LegStep::advance(float Dt) {
    if (!Active) return;
    ElapsedSec += Dt;
    if (ElapsedSec >= DurationSec) Active = false;
}

rig::LegStance LegStep::getStance(const rig::LegStance& Target) const {
    rig::LegStance Result = Target;
    if (!Active) return Result;
    Result.PelvisHeight = lerp(Start.PelvisHeight, Target.PelvisHeight, smoothStep(ElapsedSec / DurationSec));
    for (const BodyPart Foot : Feet) {
        const FootStep* Step = findStep(Foot);
        rig::FootPlacement& Placed = Result.getFoot(Foot);
        if (!Step || ElapsedSec < Step->BeginSec) {
            // Not stepping (yet): planted where it stood.
            Placed = Start.getFoot(Foot);
        } else if (ElapsedSec < Step->EndSec) {
            const float Share = (ElapsedSec - Step->BeginSec) / (Step->EndSec - Step->BeginSec);
            Placed = placeOnArc(*Step, Target.getFoot(Foot), Share);
        }
    }
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
    if (!Active) return std::nullopt;
    for (const auto& Step : Steps) {
        if (Step && ElapsedSec >= Step->BeginSec && ElapsedSec < Step->EndSec) return Step->Foot;
    }
    return std::nullopt;
}

float LegStep::getStepProgress() const {
    const std::optional<BodyPart> Foot = getSteppingFoot();
    if (!Foot) return 0.0f;
    const FootStep& Step = *findStep(*Foot);
    return (ElapsedSec - Step.BeginSec) / (Step.EndSec - Step.BeginSec);
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
        const std::optional<BodyPart> Foot = getSteppingFoot();
        if (!Foot) return;
        const FootStep& Step = *findStep(*Foot);
        const float PelvisX = Body.getPartPosition(BodyPart::Pelvis).X;
        const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
        const auto toWorld = [&](Vec2 Point) { return Vec2{PelvisX + Point.X * Facing, Point.Y}; };
        const rig::FootPlacement& To = Target.getFoot(*Foot);
        Vec2 Previous = toWorld(Step.From.Ankle);
        for (int Index = 1; Index <= ArcSegments; ++Index) {
            const float Share = static_cast<float>(Index) / static_cast<float>(ArcSegments);
            const Vec2 Point = toWorld(placeOnArc(Step, To, Share).Ankle);
            debug::drawLine(debug::Cat::TargetPose, Previous, Point);
            Previous = Point;
        }
        debug::drawCross(debug::Cat::TargetPose, toWorld(To.Ankle), TargetMarkSize);
        debug::drawText(debug::Cat::TargetPose, toWorld(To.Ankle),
                        std::format("step {} {:.0f}%", getBodyPartName(*Foot), getStepProgress() * 100.0f));
    }
}

const FootStep* LegStep::findStep(BodyPart Foot) const {
    for (const auto& Step : Steps) {
        if (Step && Step->Foot == Foot) return &*Step;
    }
    return nullptr;
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

} // namespace

} // namespace fighter::combat
