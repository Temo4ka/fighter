#include "editor/pose_ops.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "anim/layers.hpp"
#include "editor/clip_edit.hpp"

namespace fighter::editor {
namespace {

/// The pairs of joints that mirroring swaps: arms, then legs.
constexpr std::array MirrorPairs = {std::pair(BodyPart::UpperArmL, BodyPart::UpperArmR),
                                    std::pair(BodyPart::ForearmL, BodyPart::ForearmR),
                                    std::pair(BodyPart::ThighL, BodyPart::ThighR),
                                    std::pair(BodyPart::ShinL, BodyPart::ShinR),
                                    std::pair(BodyPart::FootL, BodyPart::FootR)};

} // namespace

anim::Pose copyKeyPose(const anim::Clip& Source, size_t Index) {
    if (Index >= Source.Keys.size()) return {};
    return Source.Keys[Index].Target;
}

size_t pasteKeyPose(anim::Clip& Edited, size_t Index, const anim::Pose& Copied, const rig::RigDef& Def) {
    if (Index >= Edited.Keys.size()) return 0;
    size_t Written = 0;
    for (size_t Joint = 0; Joint < BodyPartCount; ++Joint) {
        const auto Part = static_cast<BodyPart>(Joint);
        if (!Copied.hasJoint(Part) || !Edited.Keys[Index].Target.hasJoint(Part)) continue;
        setJointAngle(Edited, Index, Part, Copied.getAngle(Part), Def);
        ++Written;
    }
    anim::Pose& Target = Edited.Keys[Index].Target;
    if (Copied.HasWeapon && Target.HasWeapon) {
        const AngleRange Wrist = getWristRange(Def);
        Target.WeaponAngle = std::clamp(Copied.WeaponAngle, Wrist.Lower, Wrist.Upper);
        ++Written;
    }
    return Written;
}

size_t mirrorKey(anim::Clip& Edited, size_t Index) {
    if (Index >= Edited.Keys.size()) return 0;
    anim::Pose& Target = Edited.Keys[Index].Target;
    size_t Skipped = 0;
    for (const auto& [Left, Right] : MirrorPairs) {
        const bool HasLeft = Target.hasJoint(Left);
        const bool HasRight = Target.hasJoint(Right);
        if (HasLeft != HasRight) {
            ++Skipped;
            continue;
        }
        if (!HasLeft) continue;
        const float LeftAngle = Target.getAngle(Left);
        Target.setAngle(Left, Target.getAngle(Right));
        Target.setAngle(Right, LeftAngle);
    }
    return Skipped;
}

void mirrorClip(anim::Clip& Edited) {
    for (auto& Key : Edited.Keys) Key.Target = anim::mirrorLegs(anim::mirrorArms(Key.Target));
    Edited.Strikers = anim::mirrorArmParts(Edited.Strikers);
}

OnionKeys findOnionKeys(const anim::Clip& Source, size_t Selected, size_t Count) {
    OnionKeys Result;
    const size_t KeyCount = Source.Keys.size();
    if (Selected >= KeyCount) return Result;
    // Going round a loop stops before it comes back to the selected key.
    const size_t Reach = std::min(Count, KeyCount - 1);
    for (size_t Step = 1; Step <= Reach; ++Step) {
        if (Source.Loop) {
            Result.Previous.push_back((Selected + KeyCount - Step) % KeyCount);
            Result.Next.push_back((Selected + Step) % KeyCount);
            continue;
        }
        if (Step <= Selected) Result.Previous.push_back(Selected - Step);
        if (Selected + Step < KeyCount) Result.Next.push_back(Selected + Step);
    }
    // On a loop with few keys the two sides reach the same key: show it once,
    // on the side where it is nearer (the previous).
    std::erase_if(Result.Next, [&](size_t Key) { return std::ranges::contains(Result.Previous, Key); });
    return Result;
}

} // namespace fighter::editor
