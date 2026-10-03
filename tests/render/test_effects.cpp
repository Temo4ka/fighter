#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <variant>

#include "render/effects.hpp"

using namespace fighter;
using namespace fighter::render;
using combat::ReactionLevel;
using Catch::Approx;

namespace {

constexpr double StepSec = 1.0 / 60.0;

combat::StrikeLanded makeHit(ReactionLevel Reaction, uint8_t Victim = 1) {
    combat::StrikeLanded Hit;
    Hit.Contact.Attacker.Fighter = static_cast<uint8_t>(1 - Victim);
    Hit.Contact.Victim.Fighter = Victim;
    Hit.Contact.Point = {0.3f, 1.5f};
    Hit.Reaction = Reaction;
    return Hit;
}

combat::RenderSnapshot makeAfter(uint64_t Tick) {
    combat::RenderSnapshot After;
    After.Tick = Tick;
    After.Fighters[1].Position = {1.0f, 0.0f};
    After.Fighters[1].Parts.push_back({.Part = BodyPart::Pelvis, .Position = {1.4f, 0.3f}});
    return After;
}

size_t countCircles(const RenderList& List) {
    size_t Count = 0;
    for (const RenderItem& Item : List.getItems()) {
        CHECK(Item.Where == Layer::Effects);
        Count += std::holds_alternative<CirclePrim>(Item.What) ? 1 : 0;
    }
    return Count;
}

} // namespace

TEST_CASE("Effects: frame tick is between the previous and the current step", "[render][effects]") {
    CHECK(getFrameTick(10, 0.0f) == 9.0);
    CHECK(getFrameTick(10, 0.5f) == 9.5);
    CHECK(getFrameTick(10, 1.0f) == 10.0);
    CHECK(getFrameTick(10, 2.0f) == 10.0);   // clamped
}

TEST_CASE("Effects: a hit at or above the threshold flashes, a weaker one does not", "[render][effects]") {
    EffectsParams Params;
    Params.HitFlash.MinReaction = ReactionLevel::Flinch;
    Params.HitFlash.DurationSec = 0.1f;

    BattleEffects Effects(StepSec);
    Effects.onEvent(makeHit(ReactionLevel::Touch), makeAfter(10), Params);
    RenderList Weak;
    Effects.appendTo(Weak, getFrameTick(10, 0.5f), Params);
    CHECK(countCircles(Weak) == 0);

    Effects.onEvent(makeHit(ReactionLevel::Stagger), makeAfter(20), Params);
    RenderList Strong;
    Effects.appendTo(Strong, getFrameTick(20, 0.5f), Params);
    REQUIRE(countCircles(Strong) == 1);
    const auto& Flash = std::get<CirclePrim>(Strong.getItems()[0].What);
    CHECK(Flash.Position.X == 0.3f);
    CHECK(Flash.Position.Y == 1.5f);

    // Visible in the first frame after the step, gone after its duration.
    RenderList First;
    Effects.appendTo(First, getFrameTick(20, 0.0f), Params);
    CHECK(countCircles(First) == 1);
    RenderList Later;
    Effects.appendTo(Later, getFrameTick(20 + 6, 1.0f), Params);   // 0.117 s later
    CHECK(countCircles(Later) == 0);
}

TEST_CASE("Effects: a reaction of None flashes only when the threshold is None", "[render][effects]") {
    EffectsParams Params;
    Params.HitFlash.MinReaction = ReactionLevel::None;
    BattleEffects Effects(StepSec);
    Effects.onEvent(makeHit(ReactionLevel::None), makeAfter(5), Params);
    CHECK(Effects.getActiveCount(getFrameTick(5, 0.0f), Params) == 1);
}

TEST_CASE("Effects: camera shakes on strong hits and settles", "[render][effects]") {
    EffectsParams Params;
    Params.CameraShake = {.MinReaction = ReactionLevel::Knockback, .AmplitudeM = 0.04f, .DurationSec = 0.15f,
                          .FrequencyHz = 25.0f};
    BattleEffects Effects(StepSec);

    Effects.onEvent(makeHit(ReactionLevel::Stagger), makeAfter(10), Params);
    CHECK(Effects.getCameraOffset(getFrameTick(11, 0.5f), Params).getLength() == 0.0f);

    Effects.onEvent(makeHit(ReactionLevel::Knockdown), makeAfter(20), Params);
    float Largest = 0.0f;
    for (uint64_t Tick = 20; Tick < 29; ++Tick) {
        const Vec2 Offset = Effects.getCameraOffset(getFrameTick(Tick, 0.5f), Params);
        CHECK(Offset.getLength() <= 0.04f * 1.2f);   // within the amplitude (both axes)
        Largest = std::max(Largest, Offset.getLength());
    }
    CHECK(Largest > 0.01f);
    CHECK(Effects.getCameraOffset(getFrameTick(30, 1.0f), Params).getLength() == 0.0f);   // after 0.15 s

    // The same frame tick gives the same offset: shake follows simulation time.
    CHECK(Effects.getCameraOffset(getFrameTick(22, 0.3f), Params).X ==
          Effects.getCameraOffset(getFrameTick(22, 0.3f), Params).X);
}

TEST_CASE("Effects: dust rises where the fallen fighter's pelvis is", "[render][effects]") {
    EffectsParams Params;
    Params.Dust = {.OnKnockdown = true, .Particles = 6, .DurationSec = 0.5f, .SpreadM = 0.5f, .SizeM = 0.06f};
    BattleEffects Effects(StepSec);
    Effects.onEvent(combat::KnockedDown{.Fighter = 1}, makeAfter(40), Params);

    RenderList Start;
    Effects.appendTo(Start, getFrameTick(40, 0.0f), Params);
    REQUIRE(countCircles(Start) == 6);
    for (const RenderItem& Item : Start.getItems())
        CHECK(std::get<CirclePrim>(Item.What).Position.X == Approx(1.4f));   // the pelvis, not Position

    RenderList Spread;
    Effects.appendTo(Spread, getFrameTick(40 + 15, 0.0f), Params);
    REQUIRE(countCircles(Spread) == 6);
    float Left = 1.4f, Right = 1.4f;
    for (const RenderItem& Item : Spread.getItems()) {
        const float X = std::get<CirclePrim>(Item.What).Position.X;
        Left = std::min(Left, X);
        Right = std::max(Right, X);
        CHECK(X >= 1.4f - 0.5f);
        CHECK(X <= 1.4f + 0.5f);
    }
    CHECK(Left < 1.3f);
    CHECK(Right > 1.5f);

    RenderList Over;
    Effects.appendTo(Over, getFrameTick(40 + 31, 0.0f), Params);
    CHECK(countCircles(Over) == 0);

    Params.Dust.OnKnockdown = false;
    BattleEffects Quiet(StepSec);
    Quiet.onEvent(combat::KnockedDown{.Fighter = 1}, makeAfter(40), Params);
    CHECK(Quiet.getActiveCount(getFrameTick(40, 0.5f), Params) == 0);
}

TEST_CASE("Effects: the fighter hit last is the far one", "[render][effects]") {
    const EffectsParams Params;
    BattleEffects Effects(StepSec);
    CHECK(Effects.getFarFighter() == 1);   // tie: the right one
    Effects.onEvent(makeHit(ReactionLevel::None, 0), makeAfter(3), Params);
    CHECK(Effects.getFarFighter() == 0);
    Effects.onEvent(makeHit(ReactionLevel::None, 1), makeAfter(4), Params);
    CHECK(Effects.getFarFighter() == 1);
    Effects.onEvent(makeHit(ReactionLevel::None, 0), makeAfter(5), Params);
    Effects.clear();
    CHECK(Effects.getFarFighter() == 1);
    CHECK(Effects.getActiveCount(getFrameTick(5, 0.5f), Params) == 0);
}
