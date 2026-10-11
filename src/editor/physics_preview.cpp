#include "editor/physics_preview.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <numbers>
#include <utility>

#include "anim/layers.hpp"
#include "combat/battle.hpp"
#include "combat/moves.hpp"
#include "combat/moveset.hpp"
#include "debug/draw.hpp"
#include "editor/clip_writer.hpp"

namespace fighter::editor {
namespace {

/// A close clip is played with the opponent at this share of the move's
/// "close_range_m"; the far clip of a move with a close one at the range
/// divided by it.
constexpr float CloseRangeShare = 0.8f;
/// The arena reaches this far beyond the fighters, m (as the stand's).
constexpr float ArenaMargin = 3.0f;
/// A frame reached clip time t when its time is this close below it, s.
constexpr float ClipTimeEpsilon = 1e-5f;

/// \p View moved by \p ShiftX along the floor.
combat::FighterView shiftView(combat::FighterView View, float ShiftX);
/// \p Angle wrapped into (-pi, pi].
float wrapAngle(float Angle);

} // namespace

PhysicsPreview::PhysicsPreview(PreviewSettings NewSettings) : Settings(std::move(NewSettings)) { prepare(); }

PhysicsPreview::~PhysicsPreview() = default;
PhysicsPreview::PhysicsPreview(PhysicsPreview&& Other) noexcept = default;
PhysicsPreview& PhysicsPreview::operator=(PhysicsPreview&& Other) noexcept = default;

bool PhysicsPreview::setClip(const anim::Clip& Edited) {
    std::string Text = writeClip(Edited);
    if (Text == ClipText) return false;
    ClipText = std::move(Text);
    invalidate();
    return true;
}

bool PhysicsPreview::setWithDummy(bool Enabled) {
    if (Enabled == Settings.WithDummy) return false;
    Settings.WithDummy = Enabled;
    prepare();
    return true;
}

std::optional<size_t> PhysicsPreview::findFrame(float ClipSec) {
    for (size_t Index = 0;; ++Index) {
        const FactFrame* Frame = getFrame(Index);
        if (!Frame) break;
        if (Frame->ClipSec >= ClipSec - ClipTimeEpsilon) return Index;
    }
    if (Frames.empty()) return std::nullopt;
    return Frames.size() - 1;
}

const FactFrame* PhysicsPreview::getFrame(size_t Index) {
    while (Frames.size() <= Index && computeNext()) {
    }
    return Index < Frames.size() ? &Frames[Index] : nullptr;
}

void PhysicsPreview::prepare() {
    invalidate();
    Setup.reset();
    DistanceM.reset();
    SetupProblem.clear();
    try {
        const combat::MoveLibrary Library = combat::MoveLibrary::load(Settings.DataDir);
        std::string Reason;
        for (const combat::MoveDef& Move : Library.getMoves()) {
            const bool Far = Move.Clip == Settings.ClipName;
            if (!Far && Move.CloseClip != Settings.ClipName) continue;
            try {
                Setup = combat::prepareStand({.MoveId = Move.Id,
                                              .WeaponId = Settings.WeaponId,
                                              .WithDummy = Settings.WithDummy,
                                              .DataDir = Settings.DataDir,
                                              .ClipOverrides = {}});
            } catch (const std::runtime_error& Error) {
                Reason = Error.what();
                continue;
            }
            // At the stand's distance the move may choose its other clip.
            if (Setup->ClipName != Settings.ClipName) {
                DistanceM = Far ? Move.CloseRangeM / CloseRangeShare : Move.CloseRangeM * CloseRangeShare;
            }
            break;
        }
        if (!Setup) {
            SetupProblem = Reason.empty() ? std::format("no move of data/moves plays the clip '{}'", Settings.ClipName)
                                          : Reason;
        }
    } catch (const std::exception& Error) {
        SetupProblem = Error.what();
    }
    Problem = SetupProblem;
}

void PhysicsPreview::invalidate() {
    Fight.reset();
    Run.reset();
    Frames.clear();
    Complete = false;
    ElapsedSec = 0.0;
    OriginX = 0.0f;
    Problem = SetupProblem;
}

bool PhysicsPreview::computeNext() {
    if (Complete) return false;
    if (!Setup) {
        Complete = true;
        return false;
    }
    if (!Fight) {
        ++RunCount;
        try {
            combat::MeasureRequest Request{.MoveId = Setup->MoveId,
                                           .WeaponId = Setup->WeaponId,
                                           .WithDummy = Settings.WithDummy,
                                           .DataDir = Settings.DataDir,
                                           .ClipOverrides = {}};
            if (!ClipText.empty()) Request.ClipOverrides.emplace(Settings.ClipName, ClipText);
            Setup = combat::prepareStand(Request);
            if (DistanceM) {
                combat::BattleConfig& Config = Setup->Config;
                Config.SpawnDistanceM = *DistanceM;
                Config.Arena.HalfWidthM = std::max(Config.Arena.HalfWidthM, *DistanceM * 0.5f + ArenaMargin);
                Setup->ClipName = Settings.ClipName;
            }
            Fight = std::make_unique<combat::Battle>(Setup->Config);
            Run.emplace(*Setup, StepSec);
        } catch (const std::exception& Error) {
            Problem = Error.what();
            Fight.reset();
            Complete = true;
            return false;
        }
    }

    const combat::StandConfig& Stand = Setup->Settings;
    // The run ends itself (a timeout at the latest); the bound guards a bug.
    const double LimitSec = Stand.SettleSec + Stand.StartTimeoutSec + Stand.MoveTimeoutSec + 1.0;
    while (!Run->isFinished() && ElapsedSec < LimitSec) {
        debug::beginTick();
        Fight->update(Run->getAttackerCommands(), combat::PlayerCommands{}, StepSec);
        Run->observe(*Fight);
        ElapsedSec += StepSec;
        const combat::MoveMeasure& Measure = Run->getMeasure();
        if (!Measure.Started || Run->isFinished()) continue;
        if (Frames.empty()) OriginX = Measure.PelvisStart.X;
        const combat::RenderSnapshot& Snapshot = Fight->getSnapshot();
        const combat::FighterView& Attacker = Snapshot.Fighters[0];
        Frames.push_back({.ClipSec = Attacker.AttackClipSec,
                          .Phase = Attacker.Phase,
                          .Attacker = shiftView(Attacker, -OriginX),
                          .Dummy = shiftView(Snapshot.Fighters[1], -OriginX)});
        return true;
    }
    if (!Run->getMeasure().Problem.empty()) Problem = Run->getMeasure().Problem;
    Complete = true;
    Fight.reset();
    return false;
}

std::vector<JointError> compareToTarget(const rig::RigDef& Def, const PosedBody& Target,
                                        const combat::FighterView& Fact) {
    std::vector<JointError> Errors;
    if (Fact.Parts.size() < BodyPartCount) return Errors;
    const auto getFactAngle = [&Fact](BodyPart Part) { return Fact.Parts[static_cast<size_t>(Part)].Angle; };
    for (const rig::JointDef& Joint : Def.Joints) {
        if (Joint.Child == Def.Root || anim::getLayer(Joint.Child) == anim::Layer::Legs) continue;
        const float TargetAngle = Target.get(Joint.Child).Angle - Target.get(Joint.Parent).Angle;
        const float FactAngle = getFactAngle(Joint.Child) - getFactAngle(Joint.Parent);
        Errors.push_back({.Part = Joint.Child, .ErrorRad = wrapAngle(FactAngle - TargetAngle)});
    }
    return Errors;
}

const JointError* findWorstJoint(const std::vector<JointError>& Errors) {
    const JointError* Worst = nullptr;
    for (const JointError& Error : Errors) {
        if (!Worst || std::abs(Error.ErrorRad) > std::abs(Worst->ErrorRad)) Worst = &Error;
    }
    return Worst;
}

namespace {

combat::FighterView shiftView(combat::FighterView View, float ShiftX) {
    const Vec2 Shift{ShiftX, 0.0f};
    View.Position = View.Position + Shift;
    for (PartTransform& Part : View.Parts) Part.Position = Part.Position + Shift;
    for (PartTransform& Weapon : View.Weapons) Weapon.Position = Weapon.Position + Shift;
    return View;
}

float wrapAngle(float Angle) {
    constexpr float Pi = std::numbers::pi_v<float>;
    const float Wrapped = std::remainder(Angle, 2.0f * Pi);
    return Wrapped <= -Pi ? Wrapped + 2.0f * Pi : Wrapped;
}

} // namespace
} // namespace fighter::editor
