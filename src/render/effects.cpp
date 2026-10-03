#include "render/effects.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <variant>

#include "debug/draw.hpp"

namespace fighter::render {
namespace {

/// Effects older than this are forgotten whatever their duration.
constexpr double MaxEffectAgeSec = 6.0;
constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

const sf::Color FlashColor(255, 244, 214);
const sf::Color BlockedFlashColor(176, 214, 255);
const sf::Color DustColor(150, 132, 112);

/// The fraction of an effect's life that has passed, or nullopt when it is
/// not visible (not started yet, over, or of zero duration).
std::optional<float> getProgress(double AgeSec, float DurationSec);
float getFallX(const combat::FighterView& Fighter);
uint8_t toAlpha(float Opacity);

} // namespace

double getFrameTick(uint64_t CurrentTick, float Alpha) {
    return static_cast<double>(CurrentTick) - 1.0 + static_cast<double>(std::clamp(Alpha, 0.0f, 1.0f));
}

void BattleEffects::onEvent(const combat::BattleEvent& Event, const combat::RenderSnapshot& After,
                            const EffectsParams& Params) {
    // The event happened during the step that ended at After.Tick.
    const uint64_t StartTick = After.Tick > 0 ? After.Tick - 1 : 0;
    forgetOld(StartTick);

    if (const auto* Landed = std::get_if<combat::StrikeLanded>(&Event)) {
        const size_t Victim = Landed->Contact.Victim.Fighter;
        if (Victim < After.Fighters.size()) FarFighter = Victim;
        if (Landed->Reaction >= Params.HitFlash.MinReaction)
            Flashes.push_back({StartTick, Landed->Contact.Point, Landed->Blocked});
        if (Landed->Reaction >= Params.CameraShake.MinReaction) {
            Shakes.push_back(StartTick);
            debug::logEvent(std::format("fx: camera shake ({})", combat::getReactionLevelName(Landed->Reaction)));
        }
    } else if (const auto* Fell = std::get_if<combat::KnockedDown>(&Event)) {
        if (Params.Dust.OnKnockdown && Fell->Fighter < After.Fighters.size()) {
            DustClouds.push_back({StartTick, getFallX(After.Fighters[Fell->Fighter])});
            debug::logEvent(std::format("fx: dust under P{}", Fell->Fighter + 1));
        }
    }
}

void BattleEffects::clear() {
    FarFighter = 1;
    Flashes.clear();
    Shakes.clear();
    DustClouds.clear();
}

Vec2 BattleEffects::getCameraOffset(double FrameTick, const EffectsParams& Params) const {
    const CameraShakeParams& Shake = Params.CameraShake;
    // Overlapping shakes do not add up: the freshest one decides.
    std::optional<double> Youngest;
    for (const uint64_t Start : Shakes) {
        const double Age = getAgeSec(Start, FrameTick);
        if (!getProgress(Age, Shake.DurationSec)) continue;
        if (!Youngest || Age < *Youngest) Youngest = Age;
    }
    if (!Youngest) return {};

    const float Age = static_cast<float>(*Youngest);
    const float Envelope = 1.0f - *getProgress(*Youngest, Shake.DurationSec);
    const float Phase = TwoPi * Shake.FrequencyHz * Age;
    // Mostly sideways, a little up and down at another rate, so the motion
    // does not look like a straight line.
    return Vec2{std::sin(Phase), 0.5f * std::sin(Phase * 1.37f + 1.0f)} * (Shake.AmplitudeM * Envelope);
}

void BattleEffects::appendTo(RenderList& List, double FrameTick, const EffectsParams& Params) const {
    const HitFlashParams& FlashParams = Params.HitFlash;
    for (const Flash& Spark : Flashes) {
        const auto Progress = getProgress(getAgeSec(Spark.Tick, FrameTick), FlashParams.DurationSec);
        if (!Progress) continue;
        sf::Color Color = Spark.Blocked ? BlockedFlashColor : FlashColor;
        Color.a = toAlpha(1.0f - *Progress);
        List.add(Layer::Effects, CirclePrim{Spark.Point, FlashParams.RadiusM * (0.6f + 0.4f * *Progress), Color});
    }

    const DustParams& DustDef = Params.Dust;
    const int Count = std::max(DustDef.Particles, 0);
    for (const Dust& Cloud : DustClouds) {
        const auto Progress = getProgress(getAgeSec(Cloud.Tick, FrameTick), DustDef.DurationSec);
        if (!Progress) continue;
        const float Spread = 1.0f - (1.0f - *Progress) * (1.0f - *Progress);   // fast start, slow stop
        sf::Color Color = DustColor;
        Color.a = toAlpha(0.55f * (1.0f - *Progress));
        for (int Particle = 0; Particle < Count; ++Particle) {
            // Puffs alternate sides; each pair goes a little farther. A fixed
            // pattern rather than random numbers: the same fall looks the same.
            const float Side = Particle % 2 == 0 ? 1.0f : -1.0f;
            const float Reach = static_cast<float>(Particle / 2 + 1) / static_cast<float>((Count + 1) / 2);
            const Vec2 At{Cloud.X + Side * DustDef.SpreadM * Reach * Spread,
                          DustDef.SizeM * (0.5f + 1.5f * Spread * (1.0f - Reach * 0.5f))};
            List.add(Layer::Effects, CirclePrim{At, DustDef.SizeM * (0.5f + 0.5f * Spread), Color});
        }
    }
}

size_t BattleEffects::getActiveCount(double FrameTick, const EffectsParams& Params) const {
    size_t Count = 0;
    for (const Flash& Spark : Flashes)
        Count += getProgress(getAgeSec(Spark.Tick, FrameTick), Params.HitFlash.DurationSec) ? 1 : 0;
    for (const uint64_t Start : Shakes)
        Count += getProgress(getAgeSec(Start, FrameTick), Params.CameraShake.DurationSec) ? 1 : 0;
    for (const Dust& Cloud : DustClouds)
        Count += getProgress(getAgeSec(Cloud.Tick, FrameTick), Params.Dust.DurationSec) ? 1 : 0;
    return Count;
}

double BattleEffects::getAgeSec(uint64_t Tick, double FrameTick) const {
    return (FrameTick - static_cast<double>(Tick)) * StepSec;
}

void BattleEffects::forgetOld(uint64_t NowTick) {
    const double Now = static_cast<double>(NowTick);
    const auto IsOld = [&](uint64_t Tick) { return getAgeSec(Tick, Now) > MaxEffectAgeSec; };
    std::erase_if(Flashes, [&](const Flash& Spark) { return IsOld(Spark.Tick); });
    std::erase_if(Shakes, IsOld);
    std::erase_if(DustClouds, [&](const Dust& Cloud) { return IsOld(Cloud.Tick); });
}

namespace {

std::optional<float> getProgress(double AgeSec, float DurationSec) {
    if (DurationSec <= 0.0f || AgeSec < 0.0 || AgeSec >= DurationSec) return std::nullopt;
    return static_cast<float>(AgeSec / DurationSec);
}

float getFallX(const combat::FighterView& Fighter) {
    // The pelvis is where the body hits the floor; the reference point
    // between the feet may be far from it in a fall.
    for (const PartTransform& Part : Fighter.Parts) {
        if (Part.Part == BodyPart::Pelvis) return Part.Position.X;
    }
    return Fighter.Position.X;
}

uint8_t toAlpha(float Opacity) { return static_cast<uint8_t>(std::clamp(Opacity, 0.0f, 1.0f) * 255.0f); }

} // namespace

} // namespace fighter::render
