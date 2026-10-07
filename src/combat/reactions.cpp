#include "combat/reactions.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "combat/clip_library.hpp"
#include "core/text_file.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

void checkFieldNames(const Json& Object, std::string_view Path, std::initializer_list<std::string_view> Allowed);
const Json& getField(const Json& Object, std::string_view Path, std::string_view Key);
const Json& getObject(const Json& Object, std::string_view Key);
float readNumber(const Json& Object, std::string_view Path, std::string_view Key, float Min, float Max);
ReactionLevel readLevel(const Json& Object, std::string_view Path, std::string_view Key);
std::string joinPath(std::string_view Path, std::string_view Key);

} // namespace

bool isCoveredBy(BlockZone Zone, BodyPart Part) {
    switch (Part) {
        case BodyPart::Head: return Zone == BlockZone::High;
        case BodyPart::Torso:
        case BodyPart::UpperArmL:
        case BodyPart::ForearmL:
        case BodyPart::UpperArmR:
        case BodyPart::ForearmR: return Zone == BlockZone::Mid;
        case BodyPart::Pelvis:
        case BodyPart::ThighL:
        case BodyPart::ShinL:
        case BodyPart::FootL:
        case BodyPart::ThighR:
        case BodyPart::ShinR:
        case BodyPart::FootR: return Zone == BlockZone::Low;
        case BodyPart::Count: break;
    }
    return false;
}

float getThresholdScale(const ReactionTable& Table, float Poise, float Buildup) {
    return Poise * std::max(1.0f - Buildup * Table.ThresholdDrop, MinThresholdScale);
}

ReactionLevel chooseReactionLevel(const ReactionTable& Table, float Strength, float ThresholdScale) {
    ReactionLevel Reached = ReactionLevel::None;
    for (size_t Index = 1; Index < ReactionLevelCount; ++Index) {
        if (Strength < Table.Levels[Index].MinStrength * ThresholdScale) break;
        Reached = static_cast<ReactionLevel>(Index);
    }
    return Reached;
}

BlockRules getDefaultBlock(const ReactionTable& Table) {
    BlockRules Block;
    Block.DamageScale = Table.BlockDamageScale;
    Block.MaxLevel = Table.BlockMaxLevel;
    Block.StaminaScale = 1.0f;
    Block.Clips = {std::string(clips::BlockHigh), std::string(clips::BlockMid), std::string(clips::BlockLow)};
    for (const BlockZone Zone : {BlockZone::High, BlockZone::Mid, BlockZone::Low}) {
        auto& Parts = Block.Covers[static_cast<size_t>(Zone)];
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const auto Part = static_cast<BodyPart>(Index);
            if (isCoveredBy(Zone, Part)) Parts.push_back(Part);
        }
    }
    return Block;
}

std::optional<BlockZone> getHeightZone(const MoveDef& Move) {
    const std::string_view Height = Move.getHeight();
    if (Height == "high") return BlockZone::High;
    if (Height == "mid") return BlockZone::Mid;
    if (Height == "low") return BlockZone::Low;
    return std::nullopt;
}

bool isBlockedBy(const BlockRules& Block, BlockZone Guard, std::optional<BlockZone> Height, BodyPart Part,
                 bool OnShield) {
    if (OnShield) return true;
    if (!Height) return Block.covers(Guard, Part);
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Covered = static_cast<BodyPart>(Index);
        if (isCoveredBy(*Height, Covered) && Block.covers(Guard, Covered)) return true;
    }
    return false;
}

HitOutcome resolveHit(const ReactionTable& Table, const HitInput& Hit) {
    HitOutcome Outcome;
    const float Location = Table.Location[static_cast<size_t>(Hit.Part)];
    Outcome.Strength = Hit.Impulse / Hit.VictimMass * Location * (1.0f - Hit.Armor);
    Outcome.Damage = Outcome.Strength * Table.DamagePerStrength * Hit.MoveDamage * Hit.PowerScale;
    // The thresholds are lowered by what built up before this hit, not by
    // the hit itself.
    Outcome.Reaction =
        chooseReactionLevel(Table, Outcome.Strength, getThresholdScale(Table, Hit.Poise, Hit.Buildup));

    const BlockRules Fallback = Hit.Block ? BlockRules{} : getDefaultBlock(Table);
    const BlockRules& Block = Hit.Block ? *Hit.Block : Fallback;
    Outcome.Blocked = Hit.Guard && isBlockedBy(Block, *Hit.Guard, Hit.Height, Hit.Part, Hit.OnShield);
    if (Outcome.Blocked) {
        Outcome.Damage *= Block.DamageScale;
        Outcome.Reaction = std::min(Outcome.Reaction, Block.MaxLevel);
        Outcome.BlockStamina = Outcome.Strength * Table.BlockStaminaPerStrength * Block.StaminaScale;
    } else {
        Outcome.Reaction = std::max(Outcome.Reaction, Hit.MinReaction);
        Outcome.BuildupAdded = Outcome.Strength * Table.BuildupPerStrength;
    }
    return Outcome;
}

ReactionTable parseReactionTable(std::string_view JsonText) {
    ReactionTable Table;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        checkFieldNames(Root, "", {"location", "damage_per_strength", "levels", "buildup", "block"});

        const Json& Location = getObject(Root, "location");
        for (const auto& [Key, Value] : Location.items()) {
            if (!findBodyPart(Key)) {
                throw std::runtime_error(std::format("field 'location': unknown body part '{}'", Key));
            }
        }
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const std::string_view Name = getBodyPartName(static_cast<BodyPart>(Index));
            Table.Location[Index] = readNumber(Location, "location", Name, 0.0f, 100.0f);
        }

        Table.DamagePerStrength = readNumber(Root, "", "damage_per_strength", 0.0f, 1000.0f);

        const Json& Levels = getField(Root, "", "levels");
        if (!Levels.is_array() || Levels.size() != ReactionLevelCount - 1) {
            throw std::runtime_error(std::format(
                "field 'levels': must list {} levels, Touch to Knockdown, in this order", ReactionLevelCount - 1));
        }
        float Previous = -1.0f;
        for (size_t Index = 0; Index < Levels.size(); ++Index) {
            const std::string Path = std::format("levels[{}]", Index);
            const Json& Entry = Levels[Index];
            if (!Entry.is_object()) throw std::runtime_error(std::format("field '{}': must be an object", Path));
            checkFieldNames(Entry, Path, {"level", "min_strength", "stun_sec"});
            const ReactionLevel Expected = static_cast<ReactionLevel>(Index + 1);
            const ReactionLevel Level = readLevel(Entry, Path, "level");
            if (Level != Expected) {
                throw std::runtime_error(std::format("field '{}.level': '{}' where '{}' is expected (Touch to "
                                                     "Knockdown, each once, in this order)",
                                                     Path, getReactionLevelName(Level),
                                                     getReactionLevelName(Expected)));
            }
            ReactionTable::Level& Target = Table.Levels[Index + 1];
            Target.MinStrength = readNumber(Entry, Path, "min_strength", 0.0f, 1000.0f);
            Target.StunSec = readNumber(Entry, Path, "stun_sec", 0.0f, 60.0f);
            if (Target.MinStrength <= Previous) {
                throw std::runtime_error(std::format("field '{}.min_strength': {} must be above the previous "
                                                     "level's {}",
                                                     Path, Target.MinStrength, Previous));
            }
            Previous = Target.MinStrength;
        }

        const Json& Buildup = getObject(Root, "buildup");
        checkFieldNames(Buildup, "buildup", {"per_strength", "decay_per_sec", "threshold_drop"});
        Table.BuildupPerStrength = readNumber(Buildup, "buildup", "per_strength", 0.0f, 1000.0f);
        Table.BuildupDecayPerSec = readNumber(Buildup, "buildup", "decay_per_sec", 0.0f, 1000.0f);
        Table.ThresholdDrop = readNumber(Buildup, "buildup", "threshold_drop", 0.0f, 1.0f);

        const Json& Block = getObject(Root, "block");
        checkFieldNames(Block, "block", {"damage_scale", "max_level", "stamina_per_strength"});
        Table.BlockDamageScale = readNumber(Block, "block", "damage_scale", 0.0f, 1.0f);
        Table.BlockMaxLevel = readLevel(Block, "block", "max_level");
        Table.BlockStaminaPerStrength = readNumber(Block, "block", "stamina_per_strength", 0.0f, 1000.0f);
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Table;
}

ReactionTable loadReactionTable(const std::filesystem::path& Path) {
    try {
        return parseReactionTable(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

void checkFieldNames(const Json& Object, std::string_view Path, std::initializer_list<std::string_view> Allowed) {
    for (const auto& Field : Object.items()) {
        if (std::ranges::find(Allowed, Field.key()) == Allowed.end()) {
            throw std::runtime_error(std::format("unknown field '{}'", joinPath(Path, Field.key())));
        }
    }
}

const Json& getField(const Json& Object, std::string_view Path, std::string_view Key) {
    const auto Found = Object.find(Key);
    if (Found == Object.end()) throw std::runtime_error(std::format("missing field '{}'", joinPath(Path, Key)));
    return *Found;
}

const Json& getObject(const Json& Object, std::string_view Key) {
    const Json& Field = getField(Object, "", Key);
    if (!Field.is_object()) throw std::runtime_error(std::format("field '{}': must be an object", Key));
    return Field;
}

float readNumber(const Json& Object, std::string_view Path, std::string_view Key, float Min, float Max) {
    const Json& Field = getField(Object, Path, Key);
    if (!Field.is_number()) {
        throw std::runtime_error(std::format("field '{}': {} is not a number", joinPath(Path, Key), Field.dump()));
    }
    const auto Value = Field.get<float>();
    if (Value < Min || Value > Max) {
        throw std::runtime_error(
            std::format("field '{}': {} is outside [{}, {}]", joinPath(Path, Key), Value, Min, Max));
    }
    return Value;
}

ReactionLevel readLevel(const Json& Object, std::string_view Path, std::string_view Key) {
    const auto Name = getField(Object, Path, Key).get<std::string>();
    const std::optional<ReactionLevel> Level = findReactionLevel(Name);
    if (!Level || *Level == ReactionLevel::None) {
        throw std::runtime_error(std::format(
            "field '{}': '{}' is not one of Touch, Flinch, Stagger, Knockback, Knockdown", joinPath(Path, Key), Name));
    }
    return *Level;
}

std::string joinPath(std::string_view Path, std::string_view Key) {
    return Path.empty() ? std::string(Key) : std::format("{}.{}", Path, Key);
}

} // namespace

} // namespace fighter::combat
