#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "rig/pelvis_reaction.hpp"

using namespace fighter;
using namespace fighter::rig;
using Catch::Approx;

namespace {

constexpr float Dt = 1.0f / 60.0f;
/// A push to the left, as a fighter facing right takes a hit.
const Vec2 PushLeft{-1.0f, 0.0f};

/// Peak of |getTilt()|, getSink() and getBuckle(Leg) over \p Sec.
struct Peaks {
    float Tilt = 0.0f;
    float Sink = 0.0f;
    float Buckle = 0.0f;
};
Peaks runFor(PelvisReaction& Reaction, float Sec, size_t Leg = 0);

} // namespace

TEST_CASE("PelvisReaction peak share matches the damped spring", "[rig][pelvis_reaction]") {
    CHECK(PelvisReaction::getPeakShare(1.0f) == Approx(std::exp(-1.0f)));
    CHECK(PelvisReaction::getPeakShare(0.0f) == Approx(1.0f));
    CHECK(PelvisReaction::getPeakShare(0.5f) < PelvisReaction::getPeakShare(0.2f));
}

TEST_CASE("PelvisReaction hit excites the springs and they decay to rest", "[rig][pelvis_reaction]") {
    PelvisReaction Reaction({}, 2);
    CHECK_FALSE(Reaction.isActive());
    // A body hit a little above the pelvis.
    const auto Kick = Reaction.excite(0.3f, PushLeft, {0.1f, 0.2f}, 0);
    CHECK(Kick.Tilt > 0.0f);   // pushed back at the top: counterclockwise
    CHECK(Kick.Sink > 0.0f);
    CHECK(Reaction.isActive());
    const Peaks Seen = runFor(Reaction, 0.6f);
    // The springs reach about the asked peaks (semi-implicit steps).
    CHECK(Seen.Tilt == Approx(Kick.Tilt).epsilon(0.25));
    CHECK(Seen.Sink == Approx(Kick.Sink).epsilon(0.25));
    runFor(Reaction, 4.0f);
    CHECK_FALSE(Reaction.isActive());
    CHECK(Reaction.getTilt() == 0.0f);
    CHECK(Reaction.getSink() == 0.0f);
}

TEST_CASE("PelvisReaction low hit buckles the near knee only", "[rig][pelvis_reaction]") {
    PelvisReaction Reaction({}, 2);
    const auto Kick = Reaction.excite(0.3f, PushLeft, {0.1f, -0.45f}, 1);
    CHECK(Kick.Leg == 1);
    CHECK(Kick.Buckle > 0.0f);
    const Peaks Seen = runFor(Reaction, 0.4f, 1);
    CHECK(Seen.Buckle > 0.5f * Kick.Buckle);
    CHECK(Reaction.getBuckle(0) == 0.0f);
    // A low hit tilts the pelvis the other way: the legs go back.
    CHECK(Kick.Tilt < 0.0f);
}

TEST_CASE("PelvisReaction high hit tilts without sinking, a level hit sinks", "[rig][pelvis_reaction]") {
    PelvisReaction High({}, 2);
    PelvisReaction Level({}, 2);
    const auto Head = High.excite(0.3f, PushLeft, {0.05f, 0.65f}, 0);
    const auto Pelvis = Level.excite(0.3f, PushLeft, {0.1f, 0.02f}, 0);
    CHECK(Head.Sink == 0.0f);
    CHECK(Head.Buckle == 0.0f);
    CHECK(Pelvis.Sink > Head.Sink);
    CHECK(std::abs(Head.Tilt) > std::abs(Pelvis.Tilt));
    // A push down sinks even from above.
    PelvisReaction Down({}, 2);
    CHECK(Down.excite(0.3f, {0.0f, -1.0f}, {0.0f, 0.65f}, 0).Sink > 0.0f);
}

TEST_CASE("PelvisReaction limits, mirror and reset", "[rig][pelvis_reaction]") {
    PelvisReaction::Params Settings;
    PelvisReaction Reaction(Settings, 2);
    const auto Kick = Reaction.excite(50.0f, PushLeft, {0.0f, -0.6f}, 0);
    CHECK(std::abs(Kick.Tilt) == Approx(Settings.MaxTilt));
    CHECK(Kick.Buckle == Approx(Settings.MaxBuckle));
    for (int Step = 0; Step < 120; ++Step) {
        Reaction.advance(Dt);
        CHECK(std::abs(Reaction.getTilt()) <= Settings.MaxTilt);
        CHECK(Reaction.getSink() <= Settings.MaxSink);
        CHECK(Reaction.getBuckle(0) <= Settings.MaxBuckle);
    }
    PelvisReaction Mirrored(Settings, 2);
    Mirrored.excite(0.3f, PushLeft, {0.0f, 0.4f}, 0);
    Mirrored.advance(Dt);
    const float Before = Mirrored.getTilt();
    Mirrored.mirror();
    CHECK(Mirrored.getTilt() == Approx(-Before));
    Mirrored.reset();
    CHECK_FALSE(Mirrored.isActive());
    // No strength, no reaction; an unknown leg buckles nothing.
    CHECK_FALSE(Mirrored.excite(0.0f, PushLeft, {0.0f, -0.4f}, 0).Buckle > 0.0f);
    CHECK(Mirrored.excite(0.3f, PushLeft, {0.0f, -0.4f}, 5).Buckle == 0.0f);
}

TEST_CASE("PelvisReaction is deterministic", "[rig][pelvis_reaction]") {
    PelvisReaction First({}, 2);
    PelvisReaction Second({}, 2);
    for (int Step = 0; Step < 90; ++Step) {
        if (Step % 30 == 0) {
            First.excite(0.25f, PushLeft, {0.1f, -0.3f}, 1);
            Second.excite(0.25f, PushLeft, {0.1f, -0.3f}, 1);
        }
        First.advance(Dt);
        Second.advance(Dt);
        CHECK(First.getTilt() == Second.getTilt());
        CHECK(First.getSink() == Second.getSink());
        CHECK(First.getBuckle(1) == Second.getBuckle(1));
    }
}

namespace {

Peaks runFor(PelvisReaction& Reaction, float Sec, size_t Leg) {
    Peaks Result;
    for (float Time = 0.0f; Time < Sec; Time += Dt) {
        Reaction.advance(Dt);
        Result.Tilt = std::max(Result.Tilt, std::abs(Reaction.getTilt()));
        Result.Sink = std::max(Result.Sink, Reaction.getSink());
        Result.Buckle = std::max(Result.Buckle, Reaction.getBuckle(Leg));
    }
    return Result;
}

} // namespace
