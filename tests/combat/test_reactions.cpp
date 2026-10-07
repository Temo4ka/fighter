#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "combat/reactions.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

namespace {

/// A table with round numbers: every location 1 except the head (2).
ReactionTable makeTable() {
    ReactionTable Table;
    Table.Location.fill(1.0f);
    Table.Location[static_cast<size_t>(BodyPart::Head)] = 2.0f;
    Table.DamagePerStrength = 10.0f;
    Table.Levels[static_cast<size_t>(ReactionLevel::Touch)] = {.MinStrength = 0.5f, .StunSec = 0.0f};
    Table.Levels[static_cast<size_t>(ReactionLevel::Flinch)] = {.MinStrength = 1.0f, .StunSec = 0.1f};
    Table.Levels[static_cast<size_t>(ReactionLevel::Stagger)] = {.MinStrength = 2.0f, .StunSec = 0.3f};
    Table.Levels[static_cast<size_t>(ReactionLevel::Knockback)] = {.MinStrength = 3.0f, .StunSec = 0.5f};
    Table.Levels[static_cast<size_t>(ReactionLevel::Knockdown)] = {.MinStrength = 5.0f, .StunSec = 0.0f};
    Table.BuildupPerStrength = 1.0f;
    Table.ThresholdDrop = 0.1f;
    Table.BlockDamageScale = 0.25f;
    Table.BlockMaxLevel = ReactionLevel::Touch;
    Table.BlockStaminaPerStrength = 4.0f;
    return Table;
}

/// A hit of the given strength in m/s on the torso of a 100 kg fighter.
HitInput makeHit(float Strength, BodyPart Part = BodyPart::Torso) {
    return {.Impulse = Strength * 100.0f, .Part = Part, .VictimMass = 100.0f};
}

const std::string ValidJson = R"({
    "location": { "Head": 1.5, "Torso": 1.0, "Pelvis": 0.9, "UpperArmL": 0.6, "ForearmL": 0.5,
                  "UpperArmR": 0.6, "ForearmR": 0.5, "ThighL": 0.8, "ShinL": 0.9, "FootL": 0.6,
                  "ThighR": 0.8, "ShinR": 0.9, "FootR": 0.6 },
    "damage_per_strength": 4.0,
    "levels": [
        { "level": "Touch",     "min_strength": 0.3, "stun_sec": 0.0 },
        { "level": "Flinch",    "min_strength": 1.0, "stun_sec": 0.15 },
        { "level": "Stagger",   "min_strength": 2.0, "stun_sec": 0.35 },
        { "level": "Knockback", "min_strength": 3.5, "stun_sec": 0.5 },
        { "level": "Knockdown", "min_strength": 5.5, "stun_sec": 0.0 }
    ],
    "buildup": { "per_strength": 1.0, "decay_per_sec": 1.5, "threshold_drop": 0.08 },
    "block": { "damage_scale": 0.2, "max_level": "Touch", "stamina_per_strength": 3.0 }
})";

/// ValidJson with the first \p From replaced by \p To.
std::string editJson(const std::string& From, const std::string& To) {
    std::string Text = ValidJson;
    const size_t At = Text.find(From);
    REQUIRE(At != std::string::npos);
    return Text.replace(At, From.size(), To);
}

} // namespace

TEST_CASE("Reactions: strength is impulse over mass times location and armor", "[combat][reactions]") {
    const ReactionTable Table = makeTable();
    HitInput Hit = makeHit(1.0f, BodyPart::Head);
    Hit.Armor = 0.25f;
    const HitOutcome Outcome = resolveHit(Table, Hit);
    CHECK(Outcome.Strength == Approx(1.0f * 2.0f * 0.75f));
    CHECK(Outcome.Damage == Approx(Outcome.Strength * 10.0f));
    CHECK_FALSE(Outcome.Blocked);
    CHECK(Outcome.BuildupAdded == Approx(Outcome.Strength));
}

TEST_CASE("Reactions: damage scales with the move and the weapon", "[combat][reactions]") {
    const ReactionTable Table = makeTable();
    HitInput Hit = makeHit(2.0f);
    Hit.MoveDamage = 1.5f;
    Hit.PowerScale = 2.0f;
    CHECK(resolveHit(Table, Hit).Damage == Approx(2.0f * 10.0f * 1.5f * 2.0f));
}

TEST_CASE("Reactions: the level is the strongest threshold reached", "[combat][reactions]") {
    const ReactionTable Table = makeTable();
    CHECK(chooseReactionLevel(Table, 0.4f, 1.0f) == ReactionLevel::None);
    CHECK(chooseReactionLevel(Table, 0.5f, 1.0f) == ReactionLevel::Touch);
    CHECK(chooseReactionLevel(Table, 2.5f, 1.0f) == ReactionLevel::Stagger);
    CHECK(chooseReactionLevel(Table, 9.0f, 1.0f) == ReactionLevel::Knockdown);
    // Poise raises the thresholds.
    CHECK(chooseReactionLevel(Table, 2.5f, 1.5f) == ReactionLevel::Flinch);
}

TEST_CASE("Reactions: buildup lowers the thresholds down to a floor", "[combat][reactions]") {
    const ReactionTable Table = makeTable();
    CHECK(getThresholdScale(Table, 1.0f, 0.0f) == Approx(1.0f));
    CHECK(getThresholdScale(Table, 1.2f, 2.0f) == Approx(1.2f * 0.8f));
    CHECK(getThresholdScale(Table, 1.0f, 100.0f) == Approx(MinThresholdScale));

    HitInput Hit = makeHit(1.8f);
    CHECK(resolveHit(Table, Hit).Reaction == ReactionLevel::Flinch);
    Hit.Buildup = 2.0f;   // thresholds x0.8: Stagger at 1.6
    CHECK(resolveHit(Table, Hit).Reaction == ReactionLevel::Stagger);
}

TEST_CASE("Reactions: a clean hit is not weaker than the move's minimum", "[combat][reactions]") {
    const ReactionTable Table = makeTable();
    HitInput Hit = makeHit(0.1f);
    CHECK(resolveHit(Table, Hit).Reaction == ReactionLevel::None);
    Hit.MinReaction = ReactionLevel::Flinch;
    CHECK(resolveHit(Table, Hit).Reaction == ReactionLevel::Flinch);
}

TEST_CASE("Reactions: a block covers its zone only", "[combat][reactions]") {
    CHECK(isCoveredBy(BlockZone::High, BodyPart::Head));
    CHECK_FALSE(isCoveredBy(BlockZone::High, BodyPart::Torso));
    CHECK(isCoveredBy(BlockZone::Mid, BodyPart::Torso));
    CHECK(isCoveredBy(BlockZone::Mid, BodyPart::ForearmR));
    CHECK_FALSE(isCoveredBy(BlockZone::Mid, BodyPart::Pelvis));
    CHECK(isCoveredBy(BlockZone::Low, BodyPart::Pelvis));
    CHECK(isCoveredBy(BlockZone::Low, BodyPart::FootL));
    CHECK_FALSE(isCoveredBy(BlockZone::Low, BodyPart::Head));

    const ReactionTable Table = makeTable();
    HitInput Hit = makeHit(4.0f);
    Hit.MinReaction = ReactionLevel::Stagger;
    const HitOutcome Clean = resolveHit(Table, Hit);
    CHECK(Clean.Reaction == ReactionLevel::Knockback);

    Hit.Guard = BlockZone::Mid;
    const HitOutcome Blocked = resolveHit(Table, Hit);
    CHECK(Blocked.Blocked);
    CHECK(Blocked.Damage == Approx(Clean.Damage * 0.25f));
    // Capped, and the move's minimum does not apply to a blocked hit.
    CHECK(Blocked.Reaction == ReactionLevel::Touch);
    CHECK(Blocked.BlockStamina == Approx(4.0f * 4.0f));
    CHECK(Blocked.BuildupAdded == 0.0f);

    Hit.Guard = BlockZone::High;   // the wrong zone: a clean hit
    const HitOutcome Open = resolveHit(Table, Hit);
    CHECK_FALSE(Open.Blocked);
    CHECK(Open.Damage == Approx(Clean.Damage));
    CHECK(Open.Reaction == Clean.Reaction);
}

TEST_CASE("Reactions: parse reads every field", "[combat][reactions]") {
    const ReactionTable Table = parseReactionTable(ValidJson);
    CHECK(Table.Location[static_cast<size_t>(BodyPart::Head)] == 1.5f);
    CHECK(Table.Location[static_cast<size_t>(BodyPart::FootR)] == 0.6f);
    CHECK(Table.DamagePerStrength == 4.0f);
    CHECK(Table.getLevel(ReactionLevel::Stagger).MinStrength == 2.0f);
    CHECK(Table.getLevel(ReactionLevel::Stagger).StunSec == 0.35f);
    CHECK(Table.getLevel(ReactionLevel::Knockdown).MinStrength == 5.5f);
    CHECK(Table.BuildupPerStrength == 1.0f);
    CHECK(Table.BuildupDecayPerSec == 1.5f);
    CHECK(Table.ThresholdDrop == 0.08f);
    CHECK(Table.BlockDamageScale == 0.2f);
    CHECK(Table.BlockMaxLevel == ReactionLevel::Touch);
    CHECK(Table.BlockStaminaPerStrength == 3.0f);
}

TEST_CASE("Reactions: parse names the bad field and value", "[combat][reactions]") {
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"Head\": 1.5, ", "")), Equals("missing field 'location.Head'"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"Head\"", "\"Hed\"")),
                      Equals("field 'location': unknown body part 'Hed'"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"damage_per_strength\": 4.0", "\"damage_per_strength\": -4")),
                      Equals("field 'damage_per_strength': -4 is outside [0, 1000]"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"decay_per_sec\"", "\"decay\"")),
                      Equals("unknown field 'buildup.decay'"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"Flinch\",    \"min_strength\": 1.0",
                                                  "\"Flinch\",    \"min_strength\": 0.2")),
                      ContainsSubstring("'levels[1].min_strength': 0.2 must be above"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"level\": \"Stagger\"", "\"level\": \"Knockback\"")),
                      ContainsSubstring("'levels[2].level': 'Knockback' where 'Stagger' is expected"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"max_level\": \"Touch\"", "\"max_level\": \"Tuch\"")),
                      ContainsSubstring("field 'block.max_level': 'Tuch'"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"threshold_drop\": 0.08", "\"threshold_drop\": 2")),
                      Equals("field 'buildup.threshold_drop': 2 is outside [0, 1]"));
    CHECK_THROWS_WITH(parseReactionTable(editJson("\"block\"", "\"blocks\"")), Equals("unknown field 'blocks'"));
    CHECK_THROWS_AS(parseReactionTable("[]"), std::runtime_error);
}

TEST_CASE("Reactions: data/reactions.json loads", "[combat][reactions]") {
    const std::filesystem::path Path = std::filesystem::path(FIGHTER_DATA_DIR) / "reactions.json";
    const ReactionTable Table = loadReactionTable(Path);
    CHECK(Table.getLevel(ReactionLevel::Touch).MinStrength > 0.0f);
    CHECK_THROWS_WITH(loadReactionTable("no/such/reactions.json"), ContainsSubstring("no/such/reactions.json"));
}

TEST_CASE("Reactions: the default block is the table's with the O.2 zones", "[combat][reactions][block]") {
    const BlockRules Block = getDefaultBlock(makeTable());
    CHECK(Block.DamageScale == 0.25f);
    CHECK(Block.MaxLevel == ReactionLevel::Touch);
    CHECK(Block.StaminaScale == 1.0f);
    CHECK(Block.getClip(BlockZone::High) == "block_high");
    CHECK(Block.getClip(BlockZone::Low) == "block_low");
    CHECK(Block.covers(BlockZone::High, BodyPart::Head));
    CHECK(Block.covers(BlockZone::Mid, BodyPart::ForearmL));
    CHECK_FALSE(Block.covers(BlockZone::Mid, BodyPart::Head));
    CHECK(Block.covers(BlockZone::Low, BodyPart::ShinR));
}

TEST_CASE("Reactions: a move's height decides the block, not the part it touched", "[combat][reactions][block]") {
    MoveDef High;
    High.Tags = {"high", "punch"};
    MoveDef Plain;
    Plain.Tags = {"punch"};
    CHECK(getHeightZone(High) == BlockZone::High);
    CHECK_FALSE(getHeightZone(Plain).has_value());

    const BlockRules Block = getDefaultBlock(makeTable());
    // A high move on the forearm of a middle guard: the guard is too low.
    CHECK_FALSE(isBlockedBy(Block, BlockZone::Mid, BlockZone::High, BodyPart::ForearmL));
    CHECK(isBlockedBy(Block, BlockZone::High, BlockZone::High, BodyPart::ForearmL));
    // Without a height, the part decides.
    CHECK(isBlockedBy(Block, BlockZone::Mid, std::nullopt, BodyPart::ForearmL));
    // A low move under a high guard is not stopped; on the shield it is.
    CHECK_FALSE(isBlockedBy(Block, BlockZone::High, BlockZone::Low, BodyPart::Head));
    CHECK(isBlockedBy(Block, BlockZone::High, BlockZone::Low, BodyPart::Head, true));

    // A shield's middle guard that covers the head stops high moves.
    BlockRules Shield = Block;
    Shield.Covers[static_cast<size_t>(BlockZone::Mid)].push_back(BodyPart::Head);
    CHECK(isBlockedBy(Shield, BlockZone::Mid, BlockZone::High, BodyPart::Torso));
}

TEST_CASE("Reactions: a hit uses the victim's block", "[combat][reactions][block]") {
    const ReactionTable Table = makeTable();
    BlockRules Shield = getDefaultBlock(Table);
    Shield.DamageScale = 0.1f;
    Shield.MaxLevel = ReactionLevel::None;
    Shield.StaminaScale = 0.5f;

    HitInput Hit = makeHit(4.0f);
    const HitOutcome Clean = resolveHit(Table, Hit);
    Hit.Guard = BlockZone::Mid;
    Hit.Block = &Shield;
    const HitOutcome Blocked = resolveHit(Table, Hit);
    REQUIRE(Blocked.Blocked);
    CHECK(Blocked.Damage == Approx(Clean.Damage * 0.1f));
    CHECK(Blocked.Reaction == ReactionLevel::None);
    CHECK(Blocked.BlockStamina == Approx(4.0f * 4.0f * 0.5f));

    // A high move is not stopped by the middle guard of this block.
    Hit.Height = BlockZone::High;
    CHECK_FALSE(resolveHit(Table, Hit).Blocked);
    Hit.OnShield = true;
    CHECK(resolveHit(Table, Hit).Blocked);
}
