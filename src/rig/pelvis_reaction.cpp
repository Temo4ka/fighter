#include "rig/pelvis_reaction.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fighter::rig {
namespace {

/// Offsets and speeds below these are rest.
constexpr float RestOffset = 1e-5f;
constexpr float RestSpeed = 1e-4f;
/// A spring with 1 - z^2 below this is taken as critically damped.
constexpr float CriticalMargin = 1e-4f;

float getAngularFrequency(float Hertz);

} // namespace

PelvisReaction::Excitation PelvisReaction::excite(float Strength, Vec2 Direction, Vec2 FromPelvis, size_t NearLeg) {
    Excitation Result;
    if (Strength <= 0.0f) return Result;
    // The lever of the push about the pelvis: a hit above it turns the
    // pelvis the way it pushes, one below it the other way.
    const float Lever = cross(FromPelvis, Direction);
    Result.Tilt = std::clamp(Config.TiltPerStrength * Strength * Lever, -Config.MaxTilt, Config.MaxTilt);
    // A hit at the pelvis height goes into the legs; a push down too.
    const float Level =
        Config.SinkReach > 0.0f ? std::max(0.0f, 1.0f - std::abs(FromPelvis.Y) / Config.SinkReach) : 0.0f;
    const float SinkWeight = Level + std::max(0.0f, -Direction.Y);
    Result.Sink = std::min(Config.SinkPerStrength * Strength * SinkWeight, Config.MaxSink);
    // A hit below the pelvis buckles the knee of the nearest leg.
    const float Low = Config.BuckleReach > 0.0f ? std::clamp(-FromPelvis.Y / Config.BuckleReach, 0.0f, 1.0f)
                                                : (FromPelvis.Y < 0.0f ? 1.0f : 0.0f);
    if (NearLeg < Knees.size()) {
        Result.Leg = NearLeg;
        Result.Buckle = std::min(Config.BucklePerStrength * Strength * Low, Config.MaxBuckle);
        kick(Knees[NearLeg], Result.Buckle, Config.BuckleHertz, Config.BuckleDampingRatio);
    }
    kick(Tilt, Result.Tilt, Config.PelvisHertz, Config.PelvisDampingRatio);
    kick(Sink, Result.Sink, Config.PelvisHertz, Config.PelvisDampingRatio);
    return Result;
}

void PelvisReaction::advance(float Dt) {
    step(Tilt, Config.PelvisHertz, Config.PelvisDampingRatio, Config.MaxTilt, Dt);
    step(Sink, Config.PelvisHertz, Config.PelvisDampingRatio, Config.MaxSink, Dt);
    for (auto& Knee : Knees) step(Knee, Config.BuckleHertz, Config.BuckleDampingRatio, Config.MaxBuckle, Dt);
}

void PelvisReaction::reset() {
    Tilt = {};
    Sink = {};
    for (auto& Knee : Knees) Knee = {};
}

void PelvisReaction::mirror() {
    Tilt.Offset = -Tilt.Offset;
    Tilt.Speed = -Tilt.Speed;
}

float PelvisReaction::getSink() const { return std::max(0.0f, Sink.Offset); }

float PelvisReaction::getBuckle(size_t Leg) const {
    return Leg < Knees.size() ? std::max(0.0f, Knees[Leg].Offset) : 0.0f;
}

bool PelvisReaction::isActive() const {
    const auto moving = [](const Spring& State) {
        return std::abs(State.Offset) > RestOffset || std::abs(State.Speed) > RestSpeed;
    };
    return moving(Tilt) || moving(Sink) || std::ranges::any_of(Knees, moving);
}

float PelvisReaction::getPeakShare(float DampingRatio) {
    // x(t) = v0 / wd * exp(-z w t) sin(wd t) peaks at wd t = acos(z), where
    // x = v0 / w * exp(-z acos(z) / sqrt(1 - z^2)).
    const float Ratio = std::clamp(DampingRatio, 0.0f, 1.0f);
    if (Ratio >= 1.0f) return std::exp(-1.0f);
    return std::exp(-Ratio * std::acos(Ratio) / std::sqrt(1.0f - Ratio * Ratio));
}

void PelvisReaction::kick(Spring& State, float Peak, float Hertz, float DampingRatio) {
    if (Peak == 0.0f) return;
    State.Speed += Peak * getAngularFrequency(Hertz) / getPeakShare(DampingRatio);
}

void PelvisReaction::step(Spring& State, float Hertz, float DampingRatio, float Limit, float Dt) {
    // The exact motion of a damped spring over the step: the peaks do not
    // depend on the step length.
    const float Omega = getAngularFrequency(Hertz);
    const float Ratio = std::clamp(DampingRatio, 0.0f, 1.0f);
    const float Decay = std::exp(-Ratio * Omega * Dt);
    const float Start = State.Offset;
    const float StartSpeed = State.Speed;
    const float Undamped = 1.0f - Ratio * Ratio;
    if (Undamped < CriticalMargin) {
        // Critical: x = (A + B t) e^(-w t).
        const float Slope = StartSpeed + Omega * Start;
        State.Offset = (Start + Slope * Dt) * Decay;
        State.Speed = (Slope - Omega * (Start + Slope * Dt)) * Decay;
    } else {
        // Underdamped: x = e^(-z w t) (A cos(wd t) + B sin(wd t)).
        const float Damped = Omega * std::sqrt(Undamped);
        const float Cosine = std::cos(Damped * Dt);
        const float Sine = std::sin(Damped * Dt);
        const float Along = (StartSpeed + Ratio * Omega * Start) / Damped;
        State.Offset = Decay * (Start * Cosine + Along * Sine);
        State.Speed = Decay * ((Along * Damped - Ratio * Omega * Start) * Cosine -
                               (Start * Damped + Ratio * Omega * Along) * Sine);
    }
    if (std::abs(State.Offset) > Limit) {
        State.Offset = std::clamp(State.Offset, -Limit, Limit);
        // At the limit it stops going further out.
        if (State.Speed * State.Offset > 0.0f) State.Speed = 0.0f;
    }
    if (std::abs(State.Offset) < RestOffset && std::abs(State.Speed) < RestSpeed) State = {};
}

namespace {

float getAngularFrequency(float Hertz) { return 2.0f * std::numbers::pi_v<float> * Hertz; }

} // namespace

} // namespace fighter::rig
