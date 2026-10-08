//===- combat/scenario.hpp - Helpers for scenario tests ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the helpers the scenario tests of a fight share
/// (test_battle.cpp, test_fight.cpp): reading the snapshot, a scratch copy of
/// the data directory that a test may edit, and reaction tables owned by the
/// tests, so that tuning data/reactions.json does not break them.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <sstream>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "combat/commands.hpp"
#include "physics/world.hpp"
#include "rig/rig_def.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

namespace fighter::combat::test {

/// The three basic strikes of the "unarmed" moveset, as the tests press them.
enum class MoveButton : uint8_t { Jab, HeavyPunch, BodyKick, Count };

inline constexpr size_t MoveButtonCount = static_cast<size_t>(MoveButton::Count);

constexpr std::string_view getMoveButtonName(MoveButton Button) {
    constexpr std::array<std::string_view, MoveButtonCount> Names = {"Jab", "HeavyPunch", "BodyKick"};
    return Names[static_cast<size_t>(Button)];
}

/// Holds the input of \p Button in \p Cmd if \p Held: Light, Heavy or Kick.
constexpr void pressMove(PlayerCommands& Cmd, MoveButton Button, bool Held = true) {
    if (!Held) return;
    switch (Button) {
        case MoveButton::Jab: Cmd.Light = true; break;
        case MoveButton::HeavyPunch: Cmd.Heavy = true; break;
        case MoveButton::BodyKick: Cmd.Kick = true; break;
        case MoveButton::Count: break;
    }
}

/// Commands that hold only the input of \p Button.
constexpr PlayerCommands press(MoveButton Button) {
    PlayerCommands Cmd;
    pressMove(Cmd, Button);
    return Cmd;
}

inline constexpr double Dt = 1.0 / 60.0;
inline constexpr int TicksPerSecond = 60;
/// Distance between the fighters' floor points at which the attacker stops
/// walking and strikes, m: where the clips of task 2.2 land reliably on a
/// standing fighter with the body of task 2.1. The arms collide, so the jab
/// lands on the raised guard or, past it, on the torso up to 0.84 m between
/// the pelvises (about 0.78 m between the floor points); a fighter pushed
/// back keeps its feet planted a while, so its floor point lags behind its
/// pelvis and the attacker must stop well inside the reach not to stall out
/// of it.
inline constexpr float JabRange = 0.72f;
inline constexpr float HeavyRange = 0.8f;
/// Distance between the floor points at which P1 stops walking and jabs
/// into the raised guard (the arms collide): every jab lands on a forearm.
/// Closer, P1 walking in presses its guard into the opponent's, the limb
/// lets go (unjam) and passes through the opponent while they overlap.
inline constexpr float GuardJabRange = 0.88f;
/// The body kick lands with the foot, on the pelvis (legs hit legs, task
/// 2.1). The walk stops at the steps' floor points (0.89, 0.90, 0.92, 0.94,
/// 0.99 m from the default spawn): from 0.94 m the foot of the kick at full
/// speed (no startup floor) grazes the top of the front thigh on its way up,
/// the contact stop holds it there and it never reaches the pelvis; from
/// the others it lands. This range stops the walk at 0.90 m.
inline constexpr float KickRange = 0.92f;
/// The body kick from a little further than KickRange: the foot lands on
/// the torso (the walk stops at about 0.98 m).
inline constexpr float TorsoKickRange = 1.0f;

inline BattleConfig makeConfig() {
    BattleConfig Config;
    Config.DataDir = FIGHTER_DATA_DIR;
    return Config;
}

inline void run(Battle& Fight, const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, int Ticks) {
    for (int Tick = 0; Tick < Ticks; ++Tick) Fight.update(LeftCmd, RightCmd, Dt);
}

inline const PartTransform& getPart(const FighterView& View, BodyPart Part) {
    return View.Parts[static_cast<size_t>(Part)];
}

/// The strikes that landed during the last update().
inline std::vector<physics::HitEvent> getHits(const Battle& Fight) {
    std::vector<physics::HitEvent> Hits;
    for (const BattleEvent& Event : Fight.getEvents()) {
        if (const auto* Landed = std::get_if<StrikeLanded>(&Event)) Hits.push_back(Landed->Contact);
    }
    return Hits;
}

inline const FighterView& getLeft(const Battle& Fight) { return Fight.getSnapshot().Fighters[0]; }
inline const FighterView& getRight(const Battle& Fight) { return Fight.getSnapshot().Fighters[1]; }

inline float getPelvisX(const FighterView& View) { return getPart(View, BodyPart::Pelvis).Position.X; }
inline float getHeadHeight(const FighterView& View) { return getPart(View, BodyPart::Head).Position.Y; }

/// Upright: the head well above the floor and the torso close to vertical.
inline bool isUpright(const FighterView& View) {
    constexpr float MinHeadHeight = 1.4f;   // m; standing it is about 1.6 m
    constexpr float MaxTorsoTilt = 0.6f;    // rad
    return getHeadHeight(View) > MinHeadHeight && std::abs(getPart(View, BodyPart::Torso).Angle) < MaxTorsoTilt;
}

/// Down: the head close to the floor.
inline bool isDown(const FighterView& View) {
    constexpr float MaxHeadHeight = 0.7f;   // m
    return getHeadHeight(View) < MaxHeadHeight;
}

/// A copy of the data directory that a test may edit; removed afterwards.
class ScratchData {
public:
    explicit ScratchData(const std::string& Name)
        : Dir(std::filesystem::temp_directory_path() / ("fighter_test_" + Name)) {
        std::filesystem::remove_all(Dir);
        std::filesystem::copy(FIGHTER_DATA_DIR, Dir, std::filesystem::copy_options::recursive);
    }
    ~ScratchData() {
        std::error_code Ignored;
        std::filesystem::remove_all(Dir, Ignored);
    }
    ScratchData(const ScratchData&) = delete;
    ScratchData& operator=(const ScratchData&) = delete;

    const std::filesystem::path& getDir() const { return Dir; }

    void write(const std::filesystem::path& File, const std::string& Text) const {
        std::ofstream(Dir / File, std::ios::binary | std::ios::trunc) << Text;
    }

    /// Replaces the first \p From in \p File with \p To.
    void replace(const std::filesystem::path& File, const std::string& From, const std::string& To) const {
        std::stringstream Buffer;
        Buffer << std::ifstream(Dir / File, std::ios::binary).rdbuf();
        std::string Text = Buffer.str();
        const size_t At = Text.find(From);
        REQUIRE(At != std::string::npos);
        write(File, Text.replace(At, From.size(), To));
    }

    /// A battle config that reads this directory.
    BattleConfig makeConfig() const {
        BattleConfig Config = test::makeConfig();
        Config.DataDir = Dir;
        return Config;
    }

private:
    std::filesystem::path Dir;
};

/// A reaction table owned by a test: every location is 1, the block is as
/// in the sample file (damage x0.2, at most Touch, 3 stamina per m/s).
struct ReactionSpec {
    /// Touch, Flinch, Stagger, Knockback, Knockdown, m/s.
    std::array<float, 5> MinStrength{0.3f, 1.0f, 2.0f, 3.5f, 5.5f};
    std::array<float, 5> StunSec{0.0f, 0.15f, 0.35f, 0.5f, 0.0f};
    float BuildupPerStrength = 0.0f;
    float BuildupDecayPerSec = 1.5f;
    float ThresholdDrop = 0.0f;
};

inline std::string makeReactionsJson(const ReactionSpec& Spec) {
    std::string Location;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        Location += std::format("{}\"{}\": 1.0", Index == 0 ? "" : ", ", getBodyPartName(static_cast<BodyPart>(Index)));
    }
    std::string Levels;
    for (size_t Index = 0; Index < Spec.MinStrength.size(); ++Index) {
        Levels += std::format("{}{{\"level\": \"{}\", \"min_strength\": {}, \"stun_sec\": {}}}", Index == 0 ? "" : ", ",
                              getReactionLevelName(static_cast<ReactionLevel>(Index + 1)), Spec.MinStrength[Index],
                              Spec.StunSec[Index]);
    }
    return std::format("{{\"location\": {{{}}}, \"damage_per_strength\": 4.0, \"levels\": [{}], "
                       "\"buildup\": {{\"per_strength\": {}, \"decay_per_sec\": {}, \"threshold_drop\": {}}}, "
                       "\"block\": {{\"damage_scale\": 0.2, \"max_level\": \"Touch\", \"stamina_per_strength\": 3.0}}}}",
                       Location, Levels, Spec.BuildupPerStrength, Spec.BuildupDecayPerSec, Spec.ThresholdDrop);
}

/// Thresholds so low that a clean kick knocks the fighter down: a body kick
/// (0.5-0.6 m/s on the pelvis, less on a thigh).
inline ReactionSpec makeKnockdownKicks() { return {.MinStrength = {0.01f, 0.02f, 0.04f, 0.06f, 0.08f}}; }

/// Thresholds so high that nothing knocks the fighter down.
inline ReactionSpec makeNoKnockdowns() { return {.MinStrength = {0.3f, 1.0f, 2.0f, 3.5f, 1000.0f}}; }

inline rig::RigDef loadHumanoid() {
    return rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json");
}

/// \p Part of fighter \p Index and the posed parts of the other fighter (or
/// only \p OnlyOther) rebuilt from the rig file at their snapshot placements
/// in a probe world (as the rig places them: mirrored for the facing,
/// centered on their bounds).
struct PosedProbe {
    physics::World Probe;
    physics::Body Part;
    std::vector<physics::Body> Others;
};

inline PosedProbe makePosedProbe(const Battle& Fight, uint8_t Index, BodyPart Part,
                                 std::optional<BodyPart> OnlyOther = std::nullopt) {
    const rig::RigDef Def = loadHumanoid();
    PosedProbe Result;
    const auto addPart = [&](uint8_t Owner, BodyPart Which) {
        const FighterView& View = Fight.getSnapshot().Fighters[Owner];
        const PartTransform& Placed = View.Parts[static_cast<size_t>(Which)];
        rig::PartDef Shape = Def.getPart(Which);
        const float Facing = View.FacingRight ? 1.0f : -1.0f;
        Shape.Begin.X *= Facing;
        Shape.End.X *= Facing;
        Shape.Center.X *= Facing;
        Vec2 Low;
        Vec2 High;
        const Vec2 Radius{Shape.Radius, Shape.Radius};
        if (Shape.Shape == physics::ShapeKind::Capsule) {
            Low = Vec2{std::min(Shape.Begin.X, Shape.End.X), std::min(Shape.Begin.Y, Shape.End.Y)} - Radius;
            High = Vec2{std::max(Shape.Begin.X, Shape.End.X), std::max(Shape.Begin.Y, Shape.End.Y)} + Radius;
        } else if (Shape.Shape == physics::ShapeKind::Box) {
            Low = Shape.Center - Shape.HalfExtents;
            High = Shape.Center + Shape.HalfExtents;
        } else {
            Low = Shape.Center - Radius;
            High = Shape.Center + Radius;
        }
        const Vec2 Origin = (Low + High) * 0.5f;
        const physics::Body Handle = Result.Probe.createBody({.Type = physics::BodyType::Kinematic,
                                                              .Position = Placed.Position,
                                                              .Angle = Placed.Angle,
                                                              .Part = physics::PartRef{Owner, Which}});
        Result.Probe.addShape(Handle, {.Kind = Shape.Shape,
                                       .Center = Shape.Center - Origin,
                                       .Begin = Shape.Begin - Origin,
                                       .End = Shape.End - Origin,
                                       .HalfExtents = Shape.HalfExtents,
                                       .Radius = Shape.Radius});
        return Handle;
    };
    const auto Other = static_cast<uint8_t>(1 - Index);
    for (size_t Posed = 0; Posed < BodyPartCount; ++Posed) {
        const auto Which = static_cast<BodyPart>(Posed);
        if (Def.Kinematic.test(Posed) && (!OnlyOther || *OnlyOther == Which)) {
            Result.Others.push_back(addPart(Other, Which));
        }
    }
    Result.Part = addPart(Index, Part);
    return Result;
}

/// How deep \p Part of fighter \p Index overlaps the posed parts of the
/// other fighter in the snapshot, m; 0 if it does not touch them.
inline float getPosedPenetration(const Battle& Fight, uint8_t Index, BodyPart Part,
                                 std::optional<BodyPart> OnlyOther = std::nullopt) {
    const PosedProbe Probe = makePosedProbe(Fight, Index, Part, OnlyOther);
    return Probe.Probe.getPosedPenetration(Probe.Part);
}

/// The smallest gap between \p Part of fighter \p Index and the posed parts
/// of the other fighter in the snapshot, m; negative: how deep they overlap.
inline float getPosedGap(const Battle& Fight, uint8_t Index, BodyPart Part) {
    const PosedProbe Probe = makePosedProbe(Fight, Index, Part);
    float Smallest = 1e9f;
    for (const auto& Other : Probe.Others) {
        Smallest = std::min(Smallest, Probe.Probe.getGapAt(Probe.Part, Probe.Part.getPosition(), Probe.Part.getAngle(),
                                                           Other, Other.getPosition(), Other.getAngle()));
    }
    return Smallest;
}

inline FighterConfig loadFighter(const std::string& Name) {
    const std::filesystem::path DataDir = FIGHTER_DATA_DIR;
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const stats::ResolvedFighter Sheet =
        stats::resolveFighterSheet(stats::loadFighterSheet(DataDir / "fighters" / (Name + ".json")), Catalog);
    FighterConfig Config;
    Config.Stats = Sheet.BaseStats;
    Config.Loadout = Sheet.Gear;
    return Config;
}

} // namespace fighter::combat::test
