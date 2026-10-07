#include "anim/playback.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace fighter::anim {
namespace {

float smoothStep(float T);
std::string_view getPhaseName(const Clip& Source, float TimeSec);

} // namespace

void PoseTransition::begin(const Pose& Current, float FadeSec) {
    From = Current;
    ElapsedSec = 0.0f;
    DurationSec = FadeSec;
    Active = FadeSec > 0.0f;
}

Pose PoseTransition::step(const Pose& Target, float Dt) {
    if (!Active) return Target;
    ElapsedSec += Dt;
    if (ElapsedSec >= DurationSec) {
        Active = false;
        return Target;
    }
    return blendPoses(From, Target, smoothStep(ElapsedSec / DurationSec));
}

Pose PoseTransition::peek(const Pose& Target) const {
    if (!Active) return Target;
    return blendPoses(From, Target, smoothStep(ElapsedSec / DurationSec));
}

void PoseTransition::cancel() { Active = false; }

float PoseTransition::getWeight() const {
    return Active ? smoothStep(ElapsedSec / DurationSec) : 1.0f;
}

std::string describePlayback(const Clip& Source, float TimeSec, float Rate, const PoseTransition& Fade) {
    std::string Text = std::format("{} {:.2f}/{:.2f} s x{:.2f} {}", Source.Name, TimeSec, Source.DurationSec, Rate,
                                   getPhaseName(Source, TimeSec));
    if (Source.ActiveEndSec > Source.ActiveBeginSec) {
        Text += std::format(", startup {:.2f} s", getStartupAtRate(Source, Rate));
    }
    if (Fade.isActive()) Text += std::format(", blend {:.2f}", Fade.getWeight());
    return Text;
}

namespace {

/// 0 -> 0, 1 -> 1, flat at both ends: the fade starts and ends gently.
float smoothStep(float T) {
    const float Clamped = std::clamp(T, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

std::string_view getPhaseName(const Clip& Source, float TimeSec) {
    if (Source.ActiveEndSec > Source.ActiveBeginSec) {
        if (TimeSec < Source.ActiveBeginSec) return "startup";
        return Source.isActiveAt(TimeSec) ? "active" : "recovery";
    }
    if (Source.Loop) return "loop";
    return Source.isFinishedAt(TimeSec) ? "done" : "playing";
}

} // namespace

} // namespace fighter::anim
