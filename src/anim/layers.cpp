#include "anim/layers.hpp"

#include <array>
#include <string>
#include <utility>

namespace fighter::anim {
namespace {

/// The leg parts, left and right of each pair side by side.
constexpr std::array LegPairs = {std::pair(BodyPart::ThighL, BodyPart::ThighR),
                                 std::pair(BodyPart::ShinL, BodyPart::ShinR),
                                 std::pair(BodyPart::FootL, BodyPart::FootR)};

/// The arm parts, left and right of each pair side by side.
constexpr std::array ArmPairs = {std::pair(BodyPart::UpperArmL, BodyPart::UpperArmR),
                                 std::pair(BodyPart::ForearmL, BodyPart::ForearmR)};

void swapBits(std::bitset<BodyPartCount>& Bits, BodyPart Left, BodyPart Right);

} // namespace

std::bitset<BodyPartCount> getLegJoints() {
    std::bitset<BodyPartCount> Joints;
    for (const auto& [Left, Right] : LegPairs) {
        Joints.set(static_cast<size_t>(Left));
        Joints.set(static_cast<size_t>(Right));
    }
    return Joints;
}

Layer getLayer(BodyPart Part) {
    return getLegJoints().test(static_cast<size_t>(Part)) ? Layer::Legs : Layer::Upper;
}

bool usesLegs(const Clip& Source) {
    return !Source.Keys.empty() && (Source.Keys.front().Target.Mask & getLegJoints()).any();
}

Pose selectJoints(const Pose& Source, const std::bitset<BodyPartCount>& Joints) {
    Pose Result;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Joints.test(Index) || !Source.Mask.test(Index)) continue;
        Result.setAngle(static_cast<BodyPart>(Index), Source.Angles[Index]);
    }
    return Result;
}

Pose joinLayers(const Pose& Upper, const Pose& Legs) {
    Pose Result = selectJoints(Upper, ~getLegJoints());
    layerPose(Result, selectJoints(Legs, getLegJoints()));
    if (Upper.HasWeapon) Result.setWeaponAngle(Upper.WeaponAngle);
    if (Upper.HasWeaponOff) Result.setWeaponOffAngle(Upper.WeaponOffAngle);
    return Result;
}

BodyPart getMirroredLegPart(BodyPart Part) {
    for (const auto& [Left, Right] : LegPairs) {
        if (Part == Left) return Right;
        if (Part == Right) return Left;
    }
    return Part;
}

Pose mirrorLegs(const Pose& Source) {
    Pose Result = Source;
    for (const auto& [Left, Right] : LegPairs) {
        const auto LeftIndex = static_cast<size_t>(Left);
        const auto RightIndex = static_cast<size_t>(Right);
        std::swap(Result.Angles[LeftIndex], Result.Angles[RightIndex]);
        swapBits(Result.Mask, Left, Right);
    }
    return Result;
}

std::bitset<BodyPartCount> mirrorLegParts(const std::bitset<BodyPartCount>& Parts) {
    std::bitset<BodyPartCount> Result = Parts;
    for (const auto& [Left, Right] : LegPairs) swapBits(Result, Left, Right);
    return Result;
}

Clip mirrorClipLegs(const Clip& Source) {
    Clip Result = Source;
    Result.Name = Source.Name + std::string(MirroredSuffix);
    for (auto& Key : Result.Keys) Key.Target = mirrorLegs(Key.Target);
    Result.Strikers = mirrorLegParts(Source.Strikers);
    return Result;
}

BodyPart getMirroredArmPart(BodyPart Part) {
    for (const auto& [Left, Right] : ArmPairs) {
        if (Part == Left) return Right;
        if (Part == Right) return Left;
    }
    return Part;
}

bool usesArms(const Clip& Source) {
    std::bitset<BodyPartCount> Arms;
    for (const auto& [Left, Right] : ArmPairs) {
        Arms.set(static_cast<size_t>(Left));
        Arms.set(static_cast<size_t>(Right));
    }
    const bool Posed = !Source.Keys.empty() && (Source.Keys.front().Target.Mask & Arms).any();
    return Posed || (Source.Strikers & Arms).any();
}

Pose mirrorArms(const Pose& Source) {
    Pose Result = Source;
    for (const auto& [Left, Right] : ArmPairs) {
        std::swap(Result.Angles[static_cast<size_t>(Left)], Result.Angles[static_cast<size_t>(Right)]);
        swapBits(Result.Mask, Left, Right);
    }
    return Result;
}

BodyPart getWeaponArm(const Clip& Source, BodyPart MainForearm) {
    const BodyPart OtherForearm = getMirroredArmPart(MainForearm);
    const auto strikesWith = [&](BodyPart Forearm) {
        const BodyPart UpperArm = Forearm == BodyPart::ForearmL ? BodyPart::UpperArmL : BodyPart::UpperArmR;
        return Source.isStriker(Forearm) || Source.isStriker(UpperArm);
    };
    if (OtherForearm == MainForearm || strikesWith(MainForearm)) return MainForearm;
    return strikesWith(OtherForearm) ? OtherForearm : MainForearm;
}

Pose swapWrists(const Pose& Source) {
    Pose Result = Source;
    std::swap(Result.WeaponAngle, Result.WeaponOffAngle);
    std::swap(Result.HasWeapon, Result.HasWeaponOff);
    return Result;
}

std::bitset<BodyPartCount> mirrorArmParts(const std::bitset<BodyPartCount>& Parts) {
    std::bitset<BodyPartCount> Result = Parts;
    for (const auto& [Left, Right] : ArmPairs) swapBits(Result, Left, Right);
    return Result;
}

Clip mirrorClipArms(const Clip& Source) {
    Clip Result = Source;
    Result.Name = Source.Name + std::string(OtherHandSuffix);
    for (auto& Key : Result.Keys) Key.Target = mirrorArms(Key.Target);
    Result.Strikers = mirrorArmParts(Source.Strikers);
    return Result;
}

namespace {

void swapBits(std::bitset<BodyPartCount>& Bits, BodyPart Left, BodyPart Right) {
    const auto LeftIndex = static_cast<size_t>(Left);
    const auto RightIndex = static_cast<size_t>(Right);
    const bool LeftSet = Bits.test(LeftIndex);
    Bits.set(LeftIndex, Bits.test(RightIndex));
    Bits.set(RightIndex, LeftSet);
}

} // namespace

} // namespace fighter::anim
