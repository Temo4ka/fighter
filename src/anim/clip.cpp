#include "anim/clip.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::anim {
namespace {

using Json = nlohmann::json;

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

void checkKeys(const Json& Node, std::initializer_list<std::string_view> Known, std::string_view Where);
Pose blendPoses(const Pose& From, const Pose& To, float T);
Pose parsePose(const Json& Node);
void validateClip(const Clip& Result);

} // namespace

Pose sampleClip(const Clip& Source, float TimeSec) {
    if (Source.Keys.empty()) return {};
    if (Source.Keys.size() == 1) return Source.Keys.front().Target;

    float Time = TimeSec;
    if (Source.Loop) {
        Time = std::fmod(Time, Source.DurationSec);
        if (Time < 0.0f) Time += Source.DurationSec;
    } else {
        Time = std::clamp(Time, 0.0f, Source.DurationSec);
    }

    // The first key after Time; the one before it is the start of the segment.
    const auto Next = std::upper_bound(Source.Keys.begin(), Source.Keys.end(), Time,
                                       [](float Value, const Keyframe& Key) { return Value < Key.TimeSec; });
    if (Next == Source.Keys.end()) {
        if (!Source.Loop) return Source.Keys.back().Target;
        // Between the last key and the end of the period: blend into the first key.
        const Keyframe& Last = Source.Keys.back();
        const float Span = Source.DurationSec - Last.TimeSec;
        const float T = Span > 0.0f ? (Time - Last.TimeSec) / Span : 0.0f;
        return blendPoses(Last.Target, Source.Keys.front().Target, T);
    }

    const Keyframe& Before = *std::prev(Next);
    const float Span = Next->TimeSec - Before.TimeSec;
    const float T = Span > 0.0f ? (Time - Before.TimeSec) / Span : 0.0f;
    return blendPoses(Before.Target, Next->Target, T);
}

Clip parseClip(std::string_view JsonText, std::string Name) {
    Clip Result;
    Result.Name = std::move(Name);
    try {
        const Json Root = Json::parse(JsonText);
        checkKeys(Root, {"loop", "duration", "active", "strikers", "stiffness", "allowMove", "keys"}, "clip");
        Result.Loop = Root.value("loop", false);
        Result.DurationSec = Root.at("duration").get<float>();
        Result.Stiffness = Root.value("stiffness", 1.0f);
        Result.AllowMove = Root.value("allowMove", true);
        if (const auto Active = Root.find("active"); Active != Root.end()) {
            Result.ActiveBeginSec = Active->at(0).get<float>();
            Result.ActiveEndSec = Active->at(1).get<float>();
        }
        for (const auto& Striker : Root.value("strikers", Json::array())) {
            const std::string PartName = Striker.get<std::string>();
            const auto Part = findBodyPart(PartName);
            if (!Part) throw std::runtime_error(std::format("unknown body part '{}'", PartName));
            Result.Strikers.set(static_cast<size_t>(*Part));
        }
        for (const auto& Key : Root.at("keys")) {
            checkKeys(Key, {"t", "pose"}, "key");
            Result.Keys.push_back({.TimeSec = Key.at("t").get<float>(), .Target = parsePose(Key.at("pose"))});
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(std::format("clip '{}': {}", Result.Name, Error.what()));
    }
    validateClip(Result);
    return Result;
}

Clip loadClip(const std::filesystem::path& Path) {
    try {
        return parseClip(readTextFile(Path), Path.stem().string());
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

/// Throws if \p Node has a key that is not in \p Known: a typo in a data
/// file must not be silently ignored during live tuning.
void checkKeys(const Json& Node, std::initializer_list<std::string_view> Known, std::string_view Where) {
    if (!Node.is_object()) throw std::runtime_error(std::format("{}: expected a JSON object", Where));
    for (const auto& Item : Node.items()) {
        if (std::ranges::find(Known, Item.key()) == Known.end()) {
            throw std::runtime_error(std::format("{}: unknown key '{}'", Where, Item.key()));
        }
    }
}

Pose blendPoses(const Pose& From, const Pose& To, float T) {
    Pose Result = From;
    for (auto&& [Angle, Target] : std::views::zip(Result.Angles, To.Angles)) Angle += (Target - Angle) * T;
    return Result;
}

Pose parsePose(const Json& Node) {
    Pose Result;
    for (const auto& [PartName, Degrees] : Node.items()) {
        const auto Part = findBodyPart(PartName);
        if (!Part) throw std::runtime_error(std::format("unknown body part '{}'", PartName));
        Result.setAngle(*Part, Degrees.get<float>() * RadiansPerDegree);
    }
    return Result;
}

void validateClip(const Clip& Result) {
    auto Fail = [&](std::string_view Problem) {
        throw std::runtime_error(std::format("clip '{}': {}", Result.Name, Problem));
    };
    if (Result.Keys.empty()) Fail("no keys");
    if (Result.DurationSec <= 0.0f) Fail("duration must be positive");
    if (Result.Keys.front().TimeSec != 0.0f) Fail("the first key must be at t = 0");
    for (const auto& Key : Result.Keys) {
        if (Key.Target.Mask != Result.Keys.front().Target.Mask) Fail("every key must set the same joints");
        if (Key.TimeSec > Result.DurationSec) Fail("a key is after the end of the clip");
    }
    const bool Sorted = std::ranges::is_sorted(Result.Keys, {}, &Keyframe::TimeSec);
    if (!Sorted) Fail("keys must be sorted by time");
}

} // namespace

} // namespace fighter::anim
