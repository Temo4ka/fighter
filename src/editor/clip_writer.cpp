#include "editor/clip_writer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fighter::editor {
namespace {

constexpr double DegreesPerRadian = 180.0 / std::numbers::pi;
constexpr int AngleDecimals = 3;
constexpr int ValueDecimals = 4;

/// The order of the joints of a pose in a file.
constexpr std::array<BodyPart, BodyPartCount> JointOrder = {
    BodyPart::Pelvis,    BodyPart::Torso,    BodyPart::Head,     BodyPart::UpperArmL, BodyPart::ForearmL,
    BodyPart::UpperArmR, BodyPart::ForearmR, BodyPart::ThighL,   BodyPart::ShinL,     BodyPart::FootL,
    BodyPart::ThighR,    BodyPart::ShinR,    BodyPart::FootR,
};

/// Where the wrist goes among the joints: after the arms, before the legs.
constexpr size_t WristPosition = 7;

int getDecimalsNeeded(double Value, int MaxDecimals);
std::string formatNumber(double Value, int Decimals);
std::string formatShortest(double Value, int MaxDecimals, int MinDecimals);
std::string formatTimes(const std::vector<float>& Times, float Value);
int getTimeDecimals(const std::vector<float>& Times);
std::array<BodyPart, BodyPartCount> getJointOrder(const anim::Clip& Source);
std::string formatPose(const anim::Pose& Pose, const std::array<BodyPart, BodyPartCount>& Order);
std::string formatStrikers(const anim::Clip& Source);

} // namespace

std::string writeClip(const anim::Clip& Source) {
    std::string Text = "{\n";
    Text += std::format("  \"loop\": {},\n", Source.Loop ? "true" : "false");
    Text += std::format("  \"duration\": {},\n", formatShortest(Source.DurationSec, ValueDecimals, 1));
    if (Source.ActiveEndSec > Source.ActiveBeginSec) {
        Text += std::format("  \"active\": [{}, {}],\n", formatShortest(Source.ActiveBeginSec, ValueDecimals, 1),
                            formatShortest(Source.ActiveEndSec, ValueDecimals, 1));
    }
    if (Source.Strikers.any()) Text += std::format("  \"strikers\": {},\n", formatStrikers(Source));
    if (Source.Stiffness != 1.0f || !Source.AllowMove) {
        Text += std::format("  \"stiffness\": {},\n", formatShortest(Source.Stiffness, ValueDecimals, 1));
        Text += std::format("  \"allowMove\": {},\n", Source.AllowMove ? "true" : "false");
    }
    if (Source.BlendInSec) {
        Text += std::format("  \"blendIn\": {},\n", formatShortest(*Source.BlendInSec, ValueDecimals, 1));
    }
    if (Source.BlendOutSec) {
        Text += std::format("  \"blendOut\": {},\n", formatShortest(*Source.BlendOutSec, ValueDecimals, 1));
    }
    if (!Source.PelvisTrack.empty()) {
        std::vector<float> Times;
        for (const auto& Key : Source.PelvisTrack) Times.push_back(Key.TimeSec);
        Text += "  \"pelvisX\": [\n";
        for (auto&& [Index, Key] : std::views::zip(std::views::iota(size_t{0}), Source.PelvisTrack)) {
            Text += std::format("    {{ \"t\": {}, \"x\": {} }}{}\n", formatTimes(Times, Key.TimeSec),
                                formatShortest(Key.OffsetX, ValueDecimals, 1),
                                Index + 1 < Source.PelvisTrack.size() ? "," : "");
        }
        Text += "  ],\n";
    }
    std::vector<float> Times;
    for (const auto& Key : Source.Keys) Times.push_back(Key.TimeSec);
    const auto Order = getJointOrder(Source);
    Text += "  \"keys\": [\n";
    for (auto&& [Index, Key] : std::views::zip(std::views::iota(size_t{0}), Source.Keys)) {
        Text += std::format("    {{ \"t\": {}, \"pose\": {} }}{}\n", formatTimes(Times, Key.TimeSec),
                            formatPose(Key.Target, Order), Index + 1 < Source.Keys.size() ? "," : "");
    }
    Text += "  ]\n}\n";
    return Text;
}

std::string findClipProblem(const anim::Clip& Source) {
    try {
        anim::parseClip(writeClip(Source), Source.Name);
    } catch (const std::runtime_error& Error) {
        return Error.what();
    }
    return {};
}

void saveClip(const anim::Clip& Source, const std::filesystem::path& Path) {
    if (const std::string Problem = findClipProblem(Source); !Problem.empty()) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Problem));
    }
    const std::string Text = writeClip(Source);
    std::filesystem::path Temporary = Path;
    Temporary += ".tmp";
    {
        std::ofstream Out(Temporary, std::ios::binary | std::ios::trunc);
        Out << Text;
        if (!Out) throw std::runtime_error(std::format("{}: cannot write the file", Temporary.string()));
    }
    std::error_code Error;
    std::filesystem::rename(Temporary, Path, Error);
    if (Error) {
        throw std::runtime_error(std::format("{}: cannot replace the file: {}", Path.string(), Error.message()));
    }
}

namespace {

/// How many decimals (0..MaxDecimals) \p Value needs to be printed as it is
/// after rounding to \p MaxDecimals.
int getDecimalsNeeded(double Value, int MaxDecimals) {
    const std::string Text = formatNumber(Value, MaxDecimals);
    const size_t Point = Text.find('.');
    if (Point == std::string::npos) return 0;
    size_t Last = Text.size();
    while (Last > Point + 1 && Text[Last - 1] == '0') --Last;
    return static_cast<int>(Last - Point - 1);
}

std::string formatNumber(double Value, int Decimals) {
    std::string Text = std::format("{:.{}f}", Value, Decimals);
    // A tiny negative number rounds to "-0.00": write it without the sign.
    if (Text.starts_with('-') && Text.find_first_not_of("-0.") == std::string::npos) Text.erase(0, 1);
    return Text;
}

/// \p Value with the decimals it needs, between \p MinDecimals and \p MaxDecimals.
std::string formatShortest(double Value, int MaxDecimals, int MinDecimals) {
    return formatNumber(Value, std::max(getDecimalsNeeded(Value, MaxDecimals), MinDecimals));
}

int getTimeDecimals(const std::vector<float>& Times) {
    int Decimals = 1;
    for (const float Time : Times) Decimals = std::max(Decimals, getDecimalsNeeded(Time, ValueDecimals));
    return Decimals;
}

/// \p Value with as many decimals as the finest of \p Times needs.
std::string formatTimes(const std::vector<float>& Times, float Value) {
    return formatNumber(Value, getTimeDecimals(Times));
}

/// The joints in the order of a file, except that the arm that strikes comes
/// first when it is the right one (a weapon in the right hand: the files list
/// its arm before the other one).
std::array<BodyPart, BodyPartCount> getJointOrder(const anim::Clip& Source) {
    std::array<BodyPart, BodyPartCount> Order = JointOrder;
    const bool RightStrikes = Source.isStriker(BodyPart::UpperArmR) || Source.isStriker(BodyPart::ForearmR);
    const bool LeftStrikes = Source.isStriker(BodyPart::UpperArmL) || Source.isStriker(BodyPart::ForearmL);
    if (RightStrikes && !LeftStrikes) {
        std::swap(Order[3], Order[5]);
        std::swap(Order[4], Order[6]);
    }
    return Order;
}

std::string formatPose(const anim::Pose& Pose, const std::array<BodyPart, BodyPartCount>& Order) {
    std::vector<std::string> Entries;
    auto addJoint = [&](BodyPart Part) {
        if (!Pose.hasJoint(Part)) return;
        const double Degrees = static_cast<double>(Pose.getAngle(Part)) * DegreesPerRadian;
        Entries.push_back(std::format("\"{}\": {}", getBodyPartName(Part), formatShortest(Degrees, AngleDecimals, 0)));
    };
    for (auto&& [Index, Part] : std::views::zip(std::views::iota(size_t{0}), Order)) {
        if (Index == WristPosition && Pose.HasWeapon) {
            const double Degrees = static_cast<double>(Pose.WeaponAngle) * DegreesPerRadian;
            Entries.push_back(std::format("\"{}\": {}", anim::WeaponKey, formatShortest(Degrees, AngleDecimals, 0)));
        }
        addJoint(Part);
    }
    std::string Text = "{ ";
    for (auto&& [Index, Entry] : std::views::zip(std::views::iota(size_t{0}), Entries)) {
        if (Index > 0) Text += ", ";
        Text += Entry;
    }
    return Text + " }";
}

std::string formatStrikers(const anim::Clip& Source) {
    std::string Text = "[";
    bool First = true;
    for (const BodyPart Part : JointOrder) {
        if (!Source.isStriker(Part)) continue;
        if (!First) Text += ", ";
        Text += std::format("\"{}\"", getBodyPartName(Part));
        First = false;
    }
    return Text + "]";
}

} // namespace

} // namespace fighter::editor
