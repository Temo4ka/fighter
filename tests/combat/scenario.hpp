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

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

namespace fighter::combat::test {

inline constexpr double Dt = 1.0 / 60.0;
inline constexpr int TicksPerSecond = 60;
/// Distance between the fighters' floor points at which the attacker stops
/// walking and strikes, m (as in the fight and kick demos).
inline constexpr float JabRange = 0.72f;
inline constexpr float KickRange = 0.95f;

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

/// Thresholds so low that a clean kick knocks the fighter down.
inline ReactionSpec makeKnockdownKicks() { return {.MinStrength = {0.05f, 0.1f, 0.15f, 0.2f, 0.3f}}; }

/// Thresholds so high that nothing knocks the fighter down.
inline ReactionSpec makeNoKnockdowns() { return {.MinStrength = {0.3f, 1.0f, 2.0f, 3.5f, 1000.0f}}; }

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
