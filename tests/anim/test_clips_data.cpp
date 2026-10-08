#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "combat/moves.hpp"
#include "rig/rig_def.hpp"

using namespace fighter;
using namespace fighter::anim;
using Catch::Approx;

// Checks of the clips in data/poses (phase 2, task 2.2): they all load, they
// stay inside the joint limits of the rig, the attacks have a striking phase
// that starts inside the corridor of decision O.7, and each clip has the
// mask that its job needs.

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
/// Slack for floats that were written with a few digits in JSON, s.
constexpr float TimeEpsilon = 1e-4f;

/// Every clip of data/poses by name.
const std::map<std::string, Clip>& getClips() {
    static const std::map<std::string, Clip> Clips = [] {
        std::map<std::string, Clip> Loaded;
        for (const auto& Entry : std::filesystem::directory_iterator(DataDir / "poses")) {
            if (Entry.path().extension() != ".json") continue;
            Loaded.emplace(Entry.path().stem().string(), loadClip(Entry.path()));
        }
        return Loaded;
    }();
    return Clips;
}

const Clip& getClip(const std::string& Name) {
    const auto Found = getClips().find(Name);
    REQUIRE(Found != getClips().end());
    return Found->second;
}

bool sets(const Clip& Source, BodyPart Part) { return Source.Keys.front().Target.hasJoint(Part); }

bool setsAny(const Clip& Source, std::initializer_list<BodyPart> Parts) {
    return std::ranges::any_of(Parts, [&](BodyPart Part) { return sets(Source, Part); });
}

bool setsAll(const Clip& Source, std::initializer_list<BodyPart> Parts) {
    return std::ranges::all_of(Parts, [&](BodyPart Part) { return sets(Source, Part); });
}

const std::vector<std::string> AttackNames = {"jab",       "kick",              "heavy_punch", "jab_close",
                                              "heavy_punch_close", "sword_slash", "hammer_smash"};
const std::vector<std::string> ReactionNames = {"flinch", "stagger", "knockback"};
const std::vector<std::string> HeldNames = {"crouch", "block_high", "block_mid", "block_low"};

} // namespace

TEST_CASE("Clips: every clip of data/poses loads and the expected ones exist", "[anim][clips]") {
    const auto& Clips = getClips();
    for (const auto* Name : {"stance", "walk", "jab", "kick", "heavy_punch", "jab_close",
                             "heavy_punch_close", "sword_slash", "hammer_smash", "crouch", "block_high",
                             "block_mid", "block_low", "flinch", "stagger", "knockback"}) {
        INFO(Name);
        REQUIRE(Clips.contains(Name));
        CHECK(Clips.at(Name).Name == Name);
    }
    for (const auto& [Name, Loaded] : Clips) {
        INFO(Name);
        CHECK_FALSE(Loaded.Keys.empty());
        CHECK(Loaded.Keys.front().Target.Mask.any());
        CHECK(Loaded.Stiffness > 0.0f);
    }
}

TEST_CASE("Clips: angles stay within the joint limits of the rig", "[anim][clips]") {
    const rig::RigDef Rig = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");
    for (const auto& [Name, Source] : getClips()) {
        for (const Keyframe& Key : Source.Keys) {
            for (const rig::JointDef& Joint : Rig.Joints) {
                if (!Key.Target.hasJoint(Joint.Child)) continue;
                const float Angle = Key.Target.getAngle(Joint.Child);
                INFO(Name << " t=" << Key.TimeSec << " " << getBodyPartName(Joint.Child) << " "
                          << Angle / RadiansPerDegree << " deg");
                CHECK(Angle >= Joint.LowerAngle - 1e-4f);
                CHECK(Angle <= Joint.UpperAngle + 1e-4f);
            }
        }
    }
}

TEST_CASE("Clips: attacks have strikers and an active window", "[anim][clips]") {
    for (const auto& Name : AttackNames) {
        const Clip& Attack = getClip(Name);
        INFO(Name);
        CHECK_FALSE(Attack.Loop);
        CHECK(Attack.Strikers.any());
        CHECK(Attack.ActiveBeginSec > 0.0f);
        CHECK(Attack.ActiveEndSec > Attack.ActiveBeginSec);
        CHECK(Attack.ActiveEndSec <= Attack.DurationSec);
        // A striker must be driven by the clip, or it would just hang there.
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            if (Attack.Strikers.test(Index)) CHECK(sets(Attack, static_cast<BodyPart>(Index)));
        }
        // The striking phase is not the whole clip: there is a windup and a recovery.
        CHECK(Attack.ActiveEndSec < Attack.DurationSec - 0.1f);
    }
}

TEST_CASE("Clips: startup of the attacks is inside the corridor of O.7", "[anim][clips]") {
    // The jab starts striking 0.15-0.20 s after the button, a heavy strike
    // 0.30-0.40 s.
    struct Corridor {
        const char* Name;
        float MinSec;
        float MaxSec;
    };
    const std::vector<Corridor> Corridors = {
        {"jab", 0.15f, 0.20f},          {"jab_close", 0.15f, 0.20f},
        {"heavy_punch", 0.30f, 0.40f},  {"heavy_punch_close", 0.30f, 0.40f},
        {"sword_slash", 0.30f, 0.40f},  {"hammer_smash", 0.30f, 0.40f},
    };
    for (const auto& [Name, MinSec, MaxSec] : Corridors) {
        const Clip& Attack = getClip(Name);
        INFO(Name << " starts at " << Attack.ActiveBeginSec << " s");
        CHECK(Attack.ActiveBeginSec >= MinSec - TimeEpsilon);
        CHECK(Attack.ActiveBeginSec <= MaxSec + TimeEpsilon);
    }
}

TEST_CASE("Clips: every move names an existing clip", "[anim][clips]") {
    const auto Moves = combat::loadMoves(DataDir / "moves");
    REQUIRE_FALSE(Moves.empty());
    for (const auto& Move : Moves) {
        INFO(Move.Id << " -> clip " << Move.Clip);
        CHECK(getClips().contains(Move.Clip));
    }
}

TEST_CASE("Clips: the punches use the right arm parts", "[anim][clips]") {
    CHECK(getClip("jab").isStriker(BodyPart::ForearmL));
    CHECK(getClip("jab_close").isStriker(BodyPart::ForearmL));
    for (const auto* Name : {"heavy_punch", "heavy_punch_close", "sword_slash", "hammer_smash"}) {
        const Clip& Attack = getClip(Name);
        INFO(Name);
        // The rear hand (and the weapon held in it) strikes, nothing else.
        CHECK(Attack.isStriker(BodyPart::ForearmR));
        CHECK(Attack.Strikers.count() == 1);
        CHECK(setsAll(Attack, {BodyPart::UpperArmR, BodyPart::ForearmR}));
        CHECK_FALSE(setsAny(Attack, {BodyPart::UpperArmL, BodyPart::ForearmL}));
        CHECK_FALSE(Attack.AllowMove);
    }
}

TEST_CASE("Clips: weapon moves are slower and wider than the cross", "[anim][clips]") {
    const Clip& Cross = getClip("heavy_punch");
    for (const auto* Name : {"sword_slash", "hammer_smash"}) {
        const Clip& Weapon = getClip(Name);
        INFO(Name);
        CHECK(Weapon.DurationSec > Cross.DurationSec);
        // Wide: the upper arm travels over a larger range than in the cross.
        const auto Range = [](const Clip& Source) {
            float Lowest = 1e9f;
            float Highest = -1e9f;
            for (const Keyframe& Key : Source.Keys) {
                Lowest = std::min(Lowest, Key.Target.getAngle(BodyPart::UpperArmR));
                Highest = std::max(Highest, Key.Target.getAngle(BodyPart::UpperArmR));
            }
            return Highest - Lowest;
        };
        CHECK(Range(Weapon) > Range(Cross));
    }
}

TEST_CASE("Clips: the held poses are single-key loops with their own masks", "[anim][clips]") {
    for (const auto& Name : HeldNames) {
        const Clip& Held = getClip(Name);
        INFO(Name);
        CHECK(Held.Loop);
        CHECK(Held.Keys.size() == 1);
        CHECK(Held.Strikers.none());
        CHECK(Held.ActiveEndSec == Held.ActiveBeginSec);
    }
    const Clip& Crouch = getClip("crouch");
    // Both knees bent deeply: the pelvis follows the legs down.
    for (const auto Knee : {BodyPart::ShinL, BodyPart::ShinR}) {
        CHECK(Crouch.Keys.front().Target.getAngle(Knee) < -80.0f * RadiansPerDegree);
    }
    CHECK(setsAll(Crouch, {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR, BodyPart::ShinR,
                           BodyPart::FootR, BodyPart::Torso}));
    CHECK_FALSE(setsAny(Crouch, {BodyPart::UpperArmL, BodyPart::ForearmL, BodyPart::UpperArmR, BodyPart::ForearmR}));
    CHECK_FALSE(Crouch.AllowMove);

    for (const auto* Name : {"block_high", "block_mid"}) {
        const Clip& Block = getClip(Name);
        INFO(Name);
        CHECK(setsAll(Block, {BodyPart::UpperArmL, BodyPart::ForearmL, BodyPart::UpperArmR, BodyPart::ForearmR,
                              BodyPart::Torso, BodyPart::Head}));
        // Upper and middle guards leave the legs to walking.
        CHECK_FALSE(setsAny(Block, {BodyPart::ThighL, BodyPart::ShinL, BodyPart::ThighR, BodyPart::ShinR}));
    }
    // The low guard is a crouch with the arms down over the legs.
    const Clip& BlockLow = getClip("block_low");
    CHECK(setsAll(BlockLow, {BodyPart::ThighL, BodyPart::ShinL, BodyPart::ThighR, BodyPart::ShinR,
                             BodyPart::UpperArmL, BodyPart::ForearmL, BodyPart::UpperArmR, BodyPart::ForearmR}));
    CHECK(BlockLow.Keys.front().Target.getAngle(BodyPart::ShinL) < -80.0f * RadiansPerDegree);
}

TEST_CASE("Clips: crouch_walk is a legs-only loop that keeps the knees bent", "[anim][clips]") {
    const Clip& CrouchWalk = getClip("crouch_walk");
    CHECK(CrouchWalk.Loop);
    CHECK(CrouchWalk.Strikers.none());
    CHECK(setsAll(CrouchWalk, {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR, BodyPart::ShinR,
                               BodyPart::FootR}));
    CHECK_FALSE(setsAny(CrouchWalk, {BodyPart::Torso, BodyPart::Head, BodyPart::UpperArmL, BodyPart::UpperArmR}));
    // Sampled finely, both knees stay at least as bent as a deep squat.
    for (float Time = 0.0f; Time < CrouchWalk.DurationSec; Time += 0.01f) {
        const Pose Sampled = sampleClip(CrouchWalk, Time);
        INFO("t=" << Time);
        CHECK(Sampled.getAngle(BodyPart::ShinL) < -80.0f * RadiansPerDegree);
        CHECK(Sampled.getAngle(BodyPart::ShinR) < -80.0f * RadiansPerDegree);
    }
}

TEST_CASE("Clips: the high guard lifts the fists above the low guard", "[anim][clips]") {
    // Forearm direction in the world: torso lean + upper arm + forearm. The
    // forearm of the high guard points more upright than that of the low one.
    const auto ForearmDirection = [](const Clip& Source) {
        const Pose& Target = Source.Keys.front().Target;
        return Target.getAngle(BodyPart::Torso) + Target.getAngle(BodyPart::UpperArmL) +
               Target.getAngle(BodyPart::ForearmL);
    };
    CHECK(ForearmDirection(getClip("block_high")) > ForearmDirection(getClip("block_low")) + 1.0f);
    CHECK(getClip("block_high").Keys.front().Target.getAngle(BodyPart::UpperArmL) >
          getClip("block_mid").Keys.front().Target.getAngle(BodyPart::UpperArmL));
}

TEST_CASE("Clips: reactions are one-shot, harmless and end where they began", "[anim][clips]") {
    float PreviousDuration = 0.0f;
    for (const auto& Name : ReactionNames) {
        const Clip& Reaction = getClip(Name);
        INFO(Name);
        CHECK_FALSE(Reaction.Loop);
        CHECK(Reaction.Strikers.none());
        CHECK(Reaction.ActiveEndSec == Reaction.ActiveBeginSec);
        CHECK_FALSE(Reaction.AllowMove);
        CHECK(Reaction.Keys.size() >= 3);
        // The first and the last keys are the pose the stance has, so
        // neither the start nor the end of the clip is a jump.
        const Pose& First = Reaction.Keys.front().Target;
        const Pose& Last = Reaction.Keys.back().Target;
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            if (!First.Mask.test(Index)) continue;
            CHECK(Last.Angles[Index] == Approx(First.Angles[Index]));
        }
        // A harder hit lasts longer: flinch < stagger < knockback.
        CHECK(Reaction.DurationSec > PreviousDuration);
        PreviousDuration = Reaction.DurationSec;
    }
    CHECK(setsAll(getClip("flinch"), {BodyPart::Torso, BodyPart::Head}));
    CHECK_FALSE(setsAny(getClip("flinch"), {BodyPart::ThighL, BodyPart::ThighR}));
    // The reactions play on the upper body only (the layered walk): the legs
    // stay as they stood, the push of the hit moves the pelvis and the rig
    // steps the feet back under it.
    for (const auto& Name : ReactionNames) {
        INFO(Name);
        CHECK_FALSE(setsAny(getClip(Name), {BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL, BodyPart::ThighR,
                                            BodyPart::ShinR, BodyPart::FootR}));
    }
    const auto Recoil = [](const Clip& Source) {
        float Largest = 0.0f;
        for (const Keyframe& Key : Source.Keys) Largest = std::max(Largest, Key.Target.getAngle(BodyPart::Torso));
        return Largest;
    };
    CHECK(Recoil(getClip("flinch")) < Recoil(getClip("stagger")));
    CHECK(Recoil(getClip("stagger")) < Recoil(getClip("knockback")));
}

TEST_CASE("Clips: attacks start and end in the guard of the stance", "[anim][clips]") {
    // Layered over the stance, an attack must not make the body jump when it
    // starts or finishes: the joints it sets begin and end at the stance
    // angles. (The old kick is not re-posed yet and is left out.)
    const Pose Stance = getClip("stance").Keys.front().Target;
    for (const auto& Name : AttackNames) {
        if (Name == "kick") continue;
        const Clip& Attack = getClip(Name);
        for (const Pose* Edge : {&Attack.Keys.front().Target, &Attack.Keys.back().Target}) {
            for (size_t Index = 0; Index < BodyPartCount; ++Index) {
                if (!Edge->Mask.test(Index)) continue;
                INFO(Name << " " << getBodyPartName(static_cast<BodyPart>(Index)));
                CHECK(Edge->Angles[Index] == Approx(Stance.Angles[Index]).margin(0.5f * RadiansPerDegree));
            }
        }
    }
}

TEST_CASE("Clips: attacks do not ask joints for more speed than the motors have", "[anim][clips]") {
    // The arm motors turn at most MaxJointSpeed. A jab of the old kind asks
    // for ~19 rad/s in a short burst; no clip may ask for much more than that
    // for a long time. Legs are posed directly and have no limit.
    const rig::RigDef Rig = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");
    const float Limit = Rig.Control.MaxJointSpeed * 1.5f;
    const auto IsLeg = [](size_t Index) {
        const auto Part = static_cast<BodyPart>(Index);
        return Part == BodyPart::ThighL || Part == BodyPart::ShinL || Part == BodyPart::FootL ||
               Part == BodyPart::ThighR || Part == BodyPart::ShinR || Part == BodyPart::FootR;
    };
    for (const auto& [Name, Source] : getClips()) {
        if (Source.Loop || Source.Keys.size() < 2) continue;
        for (size_t Key = 1; Key < Source.Keys.size(); ++Key) {
            const Keyframe& Before = Source.Keys[Key - 1];
            const Keyframe& After = Source.Keys[Key];
            const float Span = After.TimeSec - Before.TimeSec;
            for (size_t Index = 0; Index < BodyPartCount; ++Index) {
                if (!After.Target.Mask.test(Index) || IsLeg(Index)) continue;
                const float Speed = std::abs(After.Target.Angles[Index] - Before.Target.Angles[Index]) / Span;
                INFO(Name << " " << getBodyPartName(static_cast<BodyPart>(Index)) << " at t=" << After.TimeSec);
                CHECK(Speed <= Limit);
            }
        }
    }
}
