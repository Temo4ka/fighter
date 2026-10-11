#include "editor/clip_check.hpp"

#include <algorithm>
#include <format>
#include <numbers>

#include "combat/data_check.hpp"
#include "editor/pose_fk.hpp"

namespace fighter::editor {
namespace {

constexpr float DegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

/// "ForearmL 142.1" or the wrist, for the text of a limit warning.
struct LimitHit {
    std::string Name;
    float AngleDeg = 0.0f;
    float LowerDeg = 0.0f;
    float UpperDeg = 0.0f;
};

/// Is \p AngleRad within \p MarginDeg of a limit of \p Range, or past it?
bool isAtLimit(float AngleRad, AngleRange Range, float MarginDeg);

} // namespace

const char* getSourceLabel(WarningSource Source) {
    switch (Source) {
        case WarningSource::Clip: return "clip";
        case WarningSource::Ghost: return "ghost";
        case WarningSource::Fact: return "fact";
    }
    return "?";
}

std::vector<ClipWarning> findTipFloorWarnings(const TipPath& Path, WarningSource Source, const EditorConfig& Config) {
    const auto Lowest = std::ranges::min_element(Path.Points, {}, [](const TipPoint& Point) { return Point.Tip.Y; });
    if (Lowest == Path.Points.end() || !(Lowest->Tip.Y < -Config.TipFloorMarginM)) return {};
    return {{.Kind = WarningKind::TipBelowFloor,
             .Source = Source,
             .TimeSec = Lowest->TimeSec,
             .Part = Path.Part,
             .Text = std::format("{}: the tip of {} is {:.1f} cm below the floor at {:.3f} s", getSourceLabel(Source),
                                 getBodyPartName(Path.Part), -Lowest->Tip.Y * 100.0f, Lowest->TimeSec)}};
}

std::vector<ClipWarning> findJointLimitWarnings(const rig::RigDef& Rig, const anim::Clip& Edited,
                                                const EditorConfig& Config) {
    std::vector<ClipWarning> Warnings;
    for (const anim::Keyframe& Key : Edited.Keys) {
        std::vector<LimitHit> Hits;
        std::optional<BodyPart> First;
        for (const rig::JointDef& Joint : Rig.Joints) {
            if (Joint.Child == Rig.Root || !Key.Target.hasJoint(Joint.Child)) continue;
            const AngleRange Range = getJointRange(Rig, Joint.Child);
            const float Angle = Key.Target.getAngle(Joint.Child);
            if (!isAtLimit(Angle, Range, Config.LimitMarginDeg)) continue;
            Hits.push_back({.Name = std::string(getBodyPartName(Joint.Child)),
                            .AngleDeg = Angle * DegreesPerRadian,
                            .LowerDeg = Range.Lower * DegreesPerRadian,
                            .UpperDeg = Range.Upper * DegreesPerRadian});
            if (!First) First = Joint.Child;
        }
        if (Key.Target.HasWeapon) {
            const AngleRange Wrist = getWristRange(Rig);
            if (isAtLimit(Key.Target.WeaponAngle, Wrist, Config.LimitMarginDeg)) {
                Hits.push_back({.Name = "wrist",
                                .AngleDeg = Key.Target.WeaponAngle * DegreesPerRadian,
                                .LowerDeg = Wrist.Lower * DegreesPerRadian,
                                .UpperDeg = Wrist.Upper * DegreesPerRadian});
            }
        }
        if (Hits.empty()) continue;
        std::string Text = std::format("clip: at limit at {:.3f} s:", Key.TimeSec);
        for (const LimitHit& Hit : Hits) {
            Text += std::format(" {} {:.1f} [{:.0f}..{:.0f}]", Hit.Name, Hit.AngleDeg, Hit.LowerDeg, Hit.UpperDeg);
        }
        Warnings.push_back({.Kind = WarningKind::JointAtLimit,
                            .Source = WarningSource::Clip,
                            .TimeSec = Key.TimeSec,
                            .Part = First,
                            .Text = std::move(Text)});
    }
    return Warnings;
}

std::vector<ClipWarning> findStrikeWarnings(const anim::Clip& Edited, bool UsesWeapon) {
    std::vector<ClipWarning> Warnings;
    const combat::StrikeProblems Problems = combat::findStrikeProblems(Edited, UsesWeapon);
    if (Problems.EmptyActive) {
        Warnings.push_back({.Kind = WarningKind::EmptyActive,
                            .Source = WarningSource::Clip,
                            .TimeSec = 0.0f,
                            .Part = std::nullopt,
                            .Text = "clip: the active phase is empty, so the move cannot hit"});
    } else if (Edited.ActiveEndSec > Edited.DurationSec) {
        Warnings.push_back({.Kind = WarningKind::EmptyActive,
                            .Source = WarningSource::Clip,
                            .TimeSec = Edited.DurationSec,
                            .Part = std::nullopt,
                            .Text = std::format("clip: the active phase ends at {:.3f} s, past the end of the clip",
                                                Edited.ActiveEndSec)});
    }
    if (Problems.NoStrikers) {
        Warnings.push_back({.Kind = WarningKind::NoStrikers,
                            .Source = WarningSource::Clip,
                            .TimeSec = Edited.ActiveBeginSec,
                            .Part = std::nullopt,
                            .Text = "clip: no striking parts, so the move cannot hit"});
    } else if (Problems.WeaponNotHeld) {
        Warnings.push_back({.Kind = WarningKind::WeaponNotHeld,
                            .Source = WarningSource::Clip,
                            .TimeSec = Edited.ActiveBeginSec,
                            .Part = std::nullopt,
                            .Text = "clip: the move strikes with the weapon, but the strikers are not a forearm"});
    }
    return Warnings;
}

std::vector<ClipWarning> findDivergenceWarnings(const GhostContext& Context, const anim::Clip& Edited,
                                                PhysicsPreview& Preview, const EditorConfig& Config) {
    const float ThresholdRad = Config.DivergenceDeg / DegreesPerRadian;
    float WorstRad = 0.0f;
    BodyPart WorstPart = BodyPart::Torso;
    float WorstTimeSec = 0.0f;
    for (size_t Index = 0; const FactFrame* Frame = Preview.getFrame(Index); ++Index) {
        if (Frame->Phase != combat::AttackPhase::Active) continue;
        const anim::Pose Ghost = composeGhostPose(Context.Stance, Edited, Frame->ClipSec, Context.WeaponHand);
        const PosedBody Target = poseBody(Context.Rig, Ghost, Context.Held);
        const std::vector<JointError> Errors = compareToTarget(Context.Rig, Target, Frame->Attacker);
        const JointError* Worst = findWorstJoint(Errors);
        if (Worst && std::abs(Worst->ErrorRad) > WorstRad) {
            WorstRad = std::abs(Worst->ErrorRad);
            WorstPart = Worst->Part;
            WorstTimeSec = Frame->ClipSec;
        }
    }
    if (!(WorstRad >= ThresholdRad)) return {};
    return {{.Kind = WarningKind::Divergence,
             .Source = WarningSource::Fact,
             .TimeSec = WorstTimeSec,
             .Part = WorstPart,
             .Text = std::format("fact: {} is {:.0f} deg off the target during the active phase at {:.3f} s",
                                 getBodyPartName(WorstPart), WorstRad * DegreesPerRadian, WorstTimeSec)}};
}

namespace {

bool isAtLimit(float AngleRad, AngleRange Range, float MarginDeg) {
    const float MarginRad = MarginDeg / DegreesPerRadian;
    return AngleRad <= Range.Lower + MarginRad || AngleRad >= Range.Upper - MarginRad;
}

} // namespace
} // namespace fighter::editor
