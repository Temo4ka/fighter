#include "editor/clip_edit.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace fighter::editor {
namespace {

/// How far after the last key a duplicate of it goes, s.
constexpr float DuplicateStepSec = 0.1f;

template <class Key>
bool isFree(const std::vector<Key>& Keys, float TimeSec);

} // namespace

std::optional<size_t> addKey(anim::Clip& Edited, float TimeSec) {
    if (TimeSec <= 0.0f || TimeSec > Edited.DurationSec || !isFree(Edited.Keys, TimeSec)) return std::nullopt;
    const anim::Pose Target = anim::sampleClip(Edited, TimeSec);
    const auto Next = std::ranges::upper_bound(Edited.Keys, TimeSec, {}, &anim::Keyframe::TimeSec);
    const auto Added = Edited.Keys.insert(Next, {.TimeSec = TimeSec, .Target = Target});
    return static_cast<size_t>(std::distance(Edited.Keys.begin(), Added));
}

bool deleteKey(anim::Clip& Edited, size_t Index) {
    if (Index == 0 || Index >= Edited.Keys.size()) return false;
    Edited.Keys.erase(Edited.Keys.begin() + static_cast<std::ptrdiff_t>(Index));
    return true;
}

std::optional<size_t> duplicateKey(anim::Clip& Edited, size_t Index) {
    if (Index >= Edited.Keys.size()) return std::nullopt;
    const float Time = Edited.Keys[Index].TimeSec;
    const bool IsLast = Index + 1 == Edited.Keys.size();
    const float Limit = IsLast ? Edited.DurationSec : Edited.Keys[Index + 1].TimeSec;
    const float NewTime = IsLast ? std::min(Time + DuplicateStepSec, Limit) : (Time + Limit) * 0.5f;
    if (NewTime - Time < MinKeyGapSec || (!IsLast && Limit - NewTime < MinKeyGapSec)) return std::nullopt;
    anim::Keyframe Copy = Edited.Keys[Index];
    Copy.TimeSec = NewTime;
    Edited.Keys.insert(Edited.Keys.begin() + static_cast<std::ptrdiff_t>(Index) + 1, Copy);
    return Index + 1;
}

float moveKey(anim::Clip& Edited, size_t Index, float TimeSec) {
    if (Index == 0 || Index >= Edited.Keys.size()) return 0.0f;
    const float Low = Edited.Keys[Index - 1].TimeSec;
    const float High = Index + 1 < Edited.Keys.size() ? Edited.Keys[Index + 1].TimeSec : Edited.DurationSec;
    Edited.Keys[Index].TimeSec = std::clamp(TimeSec, Low, std::max(Low, High));
    return Edited.Keys[Index].TimeSec;
}

float setDuration(anim::Clip& Edited, float DurationSec) {
    float Least = std::max(Edited.Keys.empty() ? 0.0f : Edited.Keys.back().TimeSec, Edited.ActiveEndSec);
    if (!Edited.PelvisTrack.empty()) Least = std::max(Least, Edited.PelvisTrack.back().TimeSec);
    Edited.DurationSec = std::max({DurationSec, Least, MinKeyGapSec});
    return Edited.DurationSec;
}

void setActive(anim::Clip& Edited, float BeginSec, float EndSec) {
    const float Begin = std::clamp(BeginSec, 0.0f, Edited.DurationSec);
    Edited.ActiveBeginSec = Begin;
    Edited.ActiveEndSec = std::clamp(EndSec, Begin, Edited.DurationSec);
}

bool isJointKeyed(const anim::Clip& Edited, BodyPart Part) {
    return !Edited.Keys.empty() && Edited.Keys.front().Target.hasJoint(Part);
}

void setJointKeyed(anim::Clip& Edited, BodyPart Part, bool Keyed, float Angle) {
    if (isJointKeyed(Edited, Part) == Keyed) return;
    for (auto& Key : Edited.Keys) {
        if (Keyed) {
            Key.Target.setAngle(Part, Angle);
        } else {
            Key.Target.Mask.reset(static_cast<size_t>(Part));
            Key.Target.Angles[static_cast<size_t>(Part)] = 0.0f;
        }
    }
}

void setWristKeyed(anim::Clip& Edited, bool Keyed, float Angle) {
    if (Edited.Keys.empty() || Edited.Keys.front().Target.HasWeapon == Keyed) return;
    for (auto& Key : Edited.Keys) {
        Key.Target.HasWeapon = Keyed;
        Key.Target.WeaponAngle = Keyed ? Angle : 0.0f;
    }
}

std::optional<size_t> addPelvisKey(anim::Clip& Edited, float TimeSec) {
    if (Edited.Loop || TimeSec <= 0.0f || TimeSec > Edited.DurationSec) return std::nullopt;
    if (Edited.PelvisTrack.empty()) Edited.PelvisTrack.push_back({.TimeSec = 0.0f, .OffsetX = 0.0f});
    if (!isFree(Edited.PelvisTrack, TimeSec)) return std::nullopt;
    const float Offset = anim::samplePelvisOffset(Edited, TimeSec);
    const auto Next = std::ranges::upper_bound(Edited.PelvisTrack, TimeSec, {}, &anim::PelvisKey::TimeSec);
    const auto Added = Edited.PelvisTrack.insert(Next, {.TimeSec = TimeSec, .OffsetX = Offset});
    return static_cast<size_t>(std::distance(Edited.PelvisTrack.begin(), Added));
}

bool deletePelvisKey(anim::Clip& Edited, size_t Index) {
    if (Index == 0 || Index >= Edited.PelvisTrack.size()) return false;
    Edited.PelvisTrack.erase(Edited.PelvisTrack.begin() + static_cast<std::ptrdiff_t>(Index));
    if (Edited.PelvisTrack.size() == 1) Edited.PelvisTrack.clear();
    return true;
}

void setPelvisKey(anim::Clip& Edited, size_t Index, float TimeSec, float OffsetX) {
    if (Index == 0 || Index >= Edited.PelvisTrack.size()) return;
    const float Low = Edited.PelvisTrack[Index - 1].TimeSec;
    const float High =
        Index + 1 < Edited.PelvisTrack.size() ? Edited.PelvisTrack[Index + 1].TimeSec : Edited.DurationSec;
    anim::PelvisKey& Key = Edited.PelvisTrack[Index];
    Key.TimeSec = std::clamp(TimeSec, Low, std::max(Low, High));
    Key.OffsetX = std::clamp(OffsetX, -anim::MaxPelvisOffsetM, anim::MaxPelvisOffsetM);
}

namespace {

/// Is no key of \p Keys closer to \p TimeSec than MinKeyGapSec?
template <class Key>
bool isFree(const std::vector<Key>& Keys, float TimeSec) {
    return std::ranges::none_of(Keys, [&](const Key& Existing) {
        return std::abs(Existing.TimeSec - TimeSec) < MinKeyGapSec;
    });
}

} // namespace

} // namespace fighter::editor
