#include "editor/trajectory.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

#include "anim/layers.hpp"
#include "editor/pose_fk.hpp"

namespace fighter::editor {
namespace {

/// The tip of \p Part in \p Body, posed by the ghost's rig.
Vec2 getGhostTip(const GhostContext& Context, const PosedBody& Body, BodyPart Part, bool UseWeapon);
/// \p Part and the part it is played as when the clip is mirrored.
BodyPart getMirroredPart(BodyPart Part);
/// The farthest the first point is from the others.
float getTravel(const std::vector<TipPoint>& Points);

} // namespace

std::vector<BodyPart> findStrikerParts(const anim::Clip& Played) {
    std::vector<BodyPart> Parts;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (Played.Strikers.test(Index)) Parts.push_back(static_cast<BodyPart>(Index));
    }
    return Parts;
}

std::optional<TipPath> sampleGhostPath(const GhostContext& Context, const anim::Clip& Edited, bool UseWeapon,
                                       float StepSec) {
    if (!(StepSec > 0.0f)) return std::nullopt;
    const anim::Clip Played = getPlayedClip(Edited, Context.WeaponHand);
    const std::vector<BodyPart> Strikers = findStrikerParts(Played);
    if (Strikers.empty()) return std::nullopt;

    std::vector<TipPath> Paths;
    for (const BodyPart Part : Strikers) Paths.push_back({.Part = Part, .Points = {}, .TravelM = 0.0f});
    // The last sample is the end of the clip, however the step divides it.
    const auto Steps = static_cast<size_t>(std::ceil(Played.DurationSec / StepSec - 1e-4f));
    for (size_t Step = 0; Step <= Steps; ++Step) {
        const float TimeSec = std::min(static_cast<float>(Step) * StepSec, Played.DurationSec);
        const anim::Pose Pose = composeGhostPose(Context.Stance, Edited, TimeSec, Context.WeaponHand);
        const PosedBody Body = poseBody(Context.Rig, Pose, Context.Held);
        const float OffsetX = anim::samplePelvisOffset(Edited, TimeSec);
        for (TipPath& Path : Paths) {
            Vec2 Tip = getGhostTip(Context, Body, Path.Part, UseWeapon && Path.Part == Context.WeaponHand);
            Tip.X += OffsetX;
            Path.Points.push_back({.TimeSec = TimeSec, .Tip = Tip, .Active = Played.isActiveAt(TimeSec)});
        }
    }
    for (TipPath& Path : Paths) Path.TravelM = getTravel(Path.Points);
    const auto Best = std::ranges::max_element(Paths, {}, &TipPath::TravelM);
    return std::move(*Best);
}

std::optional<TipPath> sampleFactPath(PhysicsPreview& Preview, BodyPart Part, bool UseWeapon) {
    std::vector<TipPath> Paths;
    for (const BodyPart Candidate : {Part, getMirroredPart(Part)}) {
        if (!Paths.empty() && Candidate == Part) continue;
        TipPath Path{.Part = Candidate, .Points = {}, .TravelM = 0.0f};
        for (size_t Index = 0; const FactFrame* Frame = Preview.getFrame(Index); ++Index) {
            if (Frame->Attacker.Parts.size() < BodyPartCount) continue;
            const Vec2 Pelvis = Frame->Attacker.Parts[static_cast<size_t>(BodyPart::Pelvis)].Position;
            const std::optional<Vec2> Weapon = UseWeapon ? combat::findWeaponTip(Frame->Attacker, Candidate) : std::nullopt;
            const Vec2 Tip = Weapon.value_or(
                combat::getPartTip(Frame->Attacker.Parts[static_cast<size_t>(Candidate)], Pelvis, 0.0f));
            Path.Points.push_back(
                {.TimeSec = Frame->ClipSec, .Tip = Tip, .Active = Frame->Phase == combat::AttackPhase::Active});
        }
        Path.TravelM = getTravel(Path.Points);
        Paths.push_back(std::move(Path));
    }
    const auto Best = std::ranges::max_element(Paths, {}, &TipPath::TravelM);
    if (Best->Points.empty()) return std::nullopt;
    return std::move(*Best);
}

TipSpeed findTipSpeed(const combat::MoveMeasure& Measure) {
    TipSpeed Speed;
    const combat::StrikerPath* Path = Measure.findFarthestPath();
    if (!Path) return Speed;
    float LengthM = 0.0f;
    float DurationSec = 0.0f;
    for (const auto& [From, To] : std::views::zip(Path->Points, Path->Points | std::views::drop(1))) {
        if (From.Phase != combat::AttackPhase::Active || To.Phase != combat::AttackPhase::Active) continue;
        const float Dt = To.TimeSec - From.TimeSec;
        if (!(Dt > 0.0f)) continue;
        const float Step = (To.Tip - From.Tip).getLength();
        Speed.PeakMps = std::max(Speed.PeakMps, Step / Dt);
        LengthM += Step;
        DurationSec += Dt;
    }
    if (DurationSec > 0.0f) Speed.ActiveMeanMps = LengthM / DurationSec;
    return Speed;
}

namespace {

Vec2 getGhostTip(const GhostContext& Context, const PosedBody& Body, BodyPart Part, bool UseWeapon) {
    if (UseWeapon) {
        for (const PosedWeapon& Weapon : Body.Weapons) {
            if (Weapon.Holder != Part) continue;
            // The surface of the blade ends one radius beyond the cap's center.
            return Weapon.Tip + (Weapon.Tip - Weapon.Fist).getNormalized() * Weapon.Radius;
        }
    }
    const rig::PartDef& Def = Context.Rig.getPart(Part);
    const PosedPart& Posed = Body.get(Part);
    const float Length = (Def.End - Def.Begin).getLength() + 2.0f * Def.Radius;
    const PartTransform Transform{
        .Part = Part, .Position = Posed.Position, .Angle = Posed.Angle, .Size = {2.0f * Def.Radius, Length}};
    return combat::getPartTip(Transform, Body.get(BodyPart::Pelvis).Position, 0.0f);
}

BodyPart getMirroredPart(BodyPart Part) {
    std::bitset<BodyPartCount> Single;
    Single.set(static_cast<size_t>(Part));
    const std::bitset<BodyPartCount> Mirrored = anim::mirrorArmParts(anim::mirrorLegParts(Single));
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (Mirrored.test(Index)) return static_cast<BodyPart>(Index);
    }
    return Part;
}

float getTravel(const std::vector<TipPoint>& Points) {
    float Travel = 0.0f;
    for (const TipPoint& Point : Points) Travel = std::max(Travel, (Point.Tip - Points.front().Tip).getLength());
    return Travel;
}

} // namespace
} // namespace fighter::editor
