//===- combat/reactions.hpp - What a hit does to the victim -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ReactionTable, the rules of data/reactions.json
/// (docs/DATA_FORMATS.md; decisions O.2, O.4 and the reaction levels of
/// docs/DEVELOPMENT_PLAN.md, agent D), and resolveHit(), which turns one
/// landed strike into its strength, damage, reaction level and block.
///
///   strength = impulse / victim mass x location[part] x (1 - armor[part])
///   damage   = strength x damage_per_strength x move damage x weapon power
///   level    = the strongest level whose min_strength x poise x
///              max(1 - buildup x threshold_drop, 0.2) <= strength,
///              not weaker than the move's min_reaction (clean hits only)
///
/// A blocked hit deals damage x block.damage_scale and its level is capped
/// at block.max_level. The block is the victim's moveset's (BlockRules,
/// task M.2); the table's "block" is its root (getDefaultBlock()). Whether a
/// guard stops a hit: isBlockedBy().
///
/// Pure logic; the fighter applies the outcome (HP, stun, buildup, stamina).
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

#include "combat/commands.hpp"
#include "combat/events.hpp"
#include "combat/moveset.hpp"
#include "core/body.hpp"

namespace fighter::combat {

struct ReactionTable {
    /// One reaction level: the strength it starts at and how long the victim
    /// cannot act.
    struct Level {
        float MinStrength = 0.0f;   ///< m/s, before the poise and the buildup.
        float StunSec = 0.0f;       ///< For Knockdown the rig's knockdownSec and getUpSec apply instead.
    };

    PerBodyPart<float> Location{};   ///< Multiplier of the strength per hit body part.
    float DamagePerStrength = 4.0f;  ///< HP per 1 m/s of strength.
    /// Indexed by ReactionLevel; None is unused (always reached).
    std::array<Level, ReactionLevelCount> Levels{};
    float BuildupPerStrength = 1.0f; ///< Buildup a hit of 1 m/s adds.
    float BuildupDecayPerSec = 1.5f; ///< Buildup lost per second.
    float ThresholdDrop = 0.08f;     ///< Fraction of the thresholds one unit of buildup takes away.
    float BlockDamageScale = 0.2f;   ///< Damage multiplier of a blocked hit.
    ReactionLevel BlockMaxLevel = ReactionLevel::Touch;   ///< The strongest reaction to a blocked hit.
    float BlockStaminaPerStrength = 3.0f;   ///< Stamina the blocker loses per 1 m/s.

    const Level& getLevel(ReactionLevel Which) const { return Levels[static_cast<size_t>(Which)]; }
};

/// The thresholds never drop below this share, however large the buildup:
/// a knockdown stays rare (decision O.7).
inline constexpr float MinThresholdScale = 0.2f;

/// Everything about one landed strike that decides what it does.
struct HitInput {
    float Impulse = 0.0f;              ///< N*s, from physics::HitEvent.
    BodyPart Part = BodyPart::Torso;   ///< The victim's part that was hit.
    float VictimMass = 1.0f;           ///< Of the whole victim, kg.
    float Armor = 0.0f;                ///< Of the hit part, 0..1.
    float Poise = 1.0f;                ///< The victim's PhysicalProfile::Poise.
    float Buildup = 0.0f;              ///< The victim's buildup before this hit.
    std::optional<BlockZone> Guard;    ///< The zone the victim blocks, if it blocks.
    /// The victim's block (its moveset's); nullptr: the table's alone
    /// (getDefaultBlock()).
    const BlockRules* Block = nullptr;
    /// The height of the move (its tag high/mid/low), if it has one.
    std::optional<BlockZone> Height;
    /// The hit landed on the shield the victim holds: blocked in any guard.
    bool OnShield = false;
    float MoveDamage = 1.0f;           ///< MoveDef::Damage.
    float PowerScale = 1.0f;           ///< WeaponProps::PowerScale for a weapon move, else 1.
    ReactionLevel MinReaction = ReactionLevel::None;   ///< MoveDef::MinReaction.
};

struct HitOutcome {
    float Strength = 0.0f;             ///< m/s.
    float Damage = 0.0f;               ///< HP, after the block.
    ReactionLevel Reaction = ReactionLevel::None;
    bool Blocked = false;
    float BuildupAdded = 0.0f;         ///< Only clean hits build up.
    float BlockStamina = 0.0f;         ///< Stamina the blocker loses.
};

/// Does a block of \p Zone cover \p Part? High: the head; Mid: the torso and
/// the arms; Low: the pelvis and the legs (decision O.2).
bool isCoveredBy(BlockZone Zone, BodyPart Part);

/// The block rules of the table alone: its damage scale and max level, a
/// stamina scale of 1, the clips block_high/mid/low and the zones of
/// decision O.2 (isCoveredBy()). The root every moveset's block inherits
/// from (MoveLibrary::getBlock()).
BlockRules getDefaultBlock(const ReactionTable& Table);

/// The height zone of a move: its tag high, mid or low (MoveDef::getHeight()),
/// or nullopt.
std::optional<BlockZone> getHeightZone(const MoveDef& Move);

/// Does \p Block guarding \p Guard stop a hit on \p Part by a move of
/// \p Height? A move with a height is stopped when the guard covers that
/// height: some part the O.2 zone of the height holds (so a shield's Mid that
/// also covers the head stops high moves); the part it touched does not
/// matter. A move without a height is stopped when the guard covers \p Part.
/// A hit on the victim's shield (\p OnShield) is stopped by any guard.
bool isBlockedBy(const BlockRules& Block, BlockZone Guard, std::optional<BlockZone> Height, BodyPart Part,
                 bool OnShield = false);

/// How much the thresholds are scaled for a victim with \p Poise and
/// \p Buildup: poise x max(1 - buildup x threshold_drop, MinThresholdScale).
float getThresholdScale(const ReactionTable& Table, float Poise, float Buildup);

/// The strongest level whose scaled threshold \p Strength reaches.
ReactionLevel chooseReactionLevel(const ReactionTable& Table, float Strength, float ThresholdScale);

/// Strength, damage, reaction and block of one strike.
HitOutcome resolveHit(const ReactionTable& Table, const HitInput& Hit);

/// Parses the table from JSON text. Every field is required, an unknown one
/// is an error. Throws std::runtime_error that names the field and the value.
ReactionTable parseReactionTable(std::string_view JsonText);

/// Reads and parses data/reactions.json. Throws std::runtime_error that
/// names the file.
ReactionTable loadReactionTable(const std::filesystem::path& Path);

} // namespace fighter::combat
