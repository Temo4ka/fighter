#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "core/body.hpp"
#include "stats/describe.hpp"
#include "stats/equipment.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"
#include "stats/stats.hpp"
#include "stats/validation.hpp"

using namespace fighter;
using namespace fighter::stats;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::StartsWith;

namespace {

const std::filesystem::path DataDir = std::filesystem::path(FIGHTER_SOURCE_DIR) / "data";

std::string readBalanceText() {
    std::ifstream File(DataDir / "balance.json", std::ios::binary);
    return {std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
}

/// data/balance.json with the first occurrence of From replaced by To.
std::string editBalanceText(std::string_view From, std::string_view To) {
    std::string Text = readBalanceText();
    const size_t Position = Text.find(From);
    REQUIRE(Position != std::string::npos);
    Text.replace(Position, From.size(), To);
    return Text;
}

PhysicalProfile profileOf(const Stats& BaseStats, const Loadout& Gear = {}) {
    return computeProfile(BaseStats, Gear, BalanceTable::getDefaults());
}

ResolvedFighter loadFighter(const std::string& Name) {
    const ItemCatalog Catalog = loadItemCatalog(DataDir / "items");
    return resolveFighterSheet(loadFighterSheet(DataDir / "fighters" / (Name + ".json")), Catalog);
}

constexpr int MinStat = MinStatValue;
constexpr int MaxStat = MaxStatValue;

} // namespace

// --- Loading -----------------------------------------------------------------

TEST_CASE("loadBalanceTable: data/balance.json equals BalanceTable::getDefaults", "[stats][balance][data]") {
    const BalanceTable Loaded = loadBalanceTable(DataDir / "balance.json");
    const BalanceTable Defaults = BalanceTable::getDefaults();

    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        INFO(getBodyPartName(static_cast<BodyPart>(Index)));
        CHECK(Loaded.BaseMassKg[Index] == Approx(Defaults.BaseMassKg[Index]));
    }
    for (const BalanceField& Field : getBalanceFields()) {
        INFO(Field.Key);
        CHECK(Loaded.*Field.Member == Approx(Defaults.*Field.Member));
    }
}

TEST_CASE("loadBalanceTable: the default table and the file produce the same profiles", "[stats][balance][data]") {
    const BalanceTable Loaded = loadBalanceTable(DataDir / "balance.json");
    const ResolvedFighter Knight = loadFighter("knight");
    const PhysicalProfile FromFile = computeProfile(Knight.BaseStats, Knight.Gear, Loaded);
    const PhysicalProfile FromCode = profileOf(Knight.BaseStats, Knight.Gear);
    CHECK(getTotalMassKg(FromFile) == Approx(getTotalMassKg(FromCode)));
    CHECK(FromFile.Poise == Approx(FromCode.Poise));
    CHECK(FromFile.AttackSpeedScale == Approx(FromCode.AttackSpeedScale));
}

TEST_CASE("getBalanceFields: lists every scalar once and the table validates", "[stats][balance]") {
    const auto Fields = getBalanceFields();
    for (size_t First = 0; First < Fields.size(); ++First) {
        for (size_t Second = First + 1; Second < Fields.size(); ++Second) {
            CHECK(Fields[First].Key != Fields[Second].Key);
            CHECK(Fields[First].Member != Fields[Second].Member);
        }
    }
    CHECK_NOTHROW(validateBalanceTable(BalanceTable::getDefaults()));
}

TEST_CASE("parseBalanceTable: the file text parses", "[stats][balance]") {
    const BalanceTable Parsed = parseBalanceTable(readBalanceText(), "balance.json");
    CHECK(Parsed.BaseHp == Approx(100.0f));
    CHECK(Parsed.BaseMassKg[static_cast<size_t>(BodyPart::Torso)] == Approx(26.0f));
}

TEST_CASE("parseBalanceTable: errors name the source, field and value", "[stats][balance]") {
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("hp_per_con": 8.0)", R"("hp_per_con": "lots")"), "b.json"),
                      Equals("b.json: field 'hp_per_con': expected a number, got string"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("hp_per_con": 8.0,)", ""), "b.json"),
                      Equals("b.json: missing field 'hp_per_con'"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("hp_per_con": 8.0,)", R"("hp_per_con": 8.0, "luck": 1,)"),
                                        "b.json"),
                      StartsWith("b.json: unknown field 'luck' (expected one of: base_mass_kg, "));
    CHECK_THROWS_WITH(parseBalanceTable("[]", "b.json"), Equals("b.json: expected an object, got array"));
    CHECK_THROWS_WITH(parseBalanceTable("{ nope", "b.json"), StartsWith("b.json: invalid JSON: "));
    CHECK_THROWS_WITH(parseBalanceTable("{}", "b.json"), Equals("b.json: base_mass_kg: missing field 'base_mass_kg'"));
}

TEST_CASE("parseBalanceTable: base_mass_kg needs every body part and nothing else", "[stats][balance]") {
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("Head": 5.0,)", ""), "b.json"),
                      Equals("b.json: base_mass_kg: missing field 'Head'"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("Head": 5.0,)", R"("Head": 5.0, "Tail": 1.0,)"), "b.json"),
                      StartsWith("b.json: base_mass_kg: unknown body part 'Tail' (expected one of: Head, Torso, "));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("Head": 5.0)", R"("Head": "heavy")"), "b.json"),
                      Equals("b.json: base_mass_kg: field 'Head': expected a number, got string"));
    CHECK_THROWS_WITH(parseBalanceTable(R"({"base_mass_kg": 5})", "b.json"),
                      Equals("b.json: base_mass_kg: expected an object, got number"));
}

TEST_CASE("parseBalanceTable: values out of range are rejected with the field", "[stats][balance]") {
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("Torso": 26.0)", R"("Torso": 0.0)"), "b.json"),
                      Equals("b.json: field 'base_mass_kg.Torso': 0 must be positive"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("base_hp": 100.0)", R"("base_hp": -5)"), "b.json"),
                      Equals("b.json: field 'base_hp': -5 must be positive"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("poise_per_armor": 1.5)", R"("poise_per_armor": -1)"),
                                        "b.json"),
                      Equals("b.json: field 'poise_per_armor': -1 must be not negative"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("attack_speed_min": 0.75)", R"("attack_speed_min": 2)"),
                                        "b.json"),
                      Equals("b.json: field 'attack_speed_min': 2 is above 'attack_speed_max' 1.25"));
    CHECK_THROWS_WITH(parseBalanceTable(editBalanceText(R"("max_part_armor": 0.9)", R"("max_part_armor": 1.5)"),
                                        "b.json"),
                      Equals("b.json: field 'max_part_armor': 1.5 is out of (0, 1]"));
}

TEST_CASE("validateBalanceTable: rejects NaN and infinity", "[stats][balance]") {
    BalanceTable Table = BalanceTable::getDefaults();
    Table.BaseHp = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(validateBalanceTable(Table), DataError);
    Table = BalanceTable::getDefaults();
    Table.HpPerCon = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH(validateBalanceTable(Table), ContainsSubstring("hp_per_con"));
}

TEST_CASE("loadBalanceTable: a missing file is reported with its path", "[stats][balance]") {
    const std::string Missing = (std::filesystem::temp_directory_path() / "fighter_no_such_balance.json").string();
    CHECK_THROWS_WITH(loadBalanceTable(Missing), Equals(Missing + ": cannot open the file"));
}

// --- Formulas ----------------------------------------------------------------

TEST_CASE("computeProfile: every stat moves its parameters monotonically", "[stats][balance]") {
    // Walk the whole validated stat range; nothing may ever go the wrong way.
    for (int Value = MinStat; Value < MaxStat; ++Value) {
        INFO("stat " << Value << " -> " << Value + 1);
        const PhysicalProfile StrLow = profileOf({.Strength = Value});
        const PhysicalProfile StrHigh = profileOf({.Strength = Value + 1});
        CHECK(StrHigh.MotorMaxTorque > StrLow.MotorMaxTorque);

        const PhysicalProfile DexLow = profileOf({.Dexterity = Value});
        const PhysicalProfile DexHigh = profileOf({.Dexterity = Value + 1});
        CHECK(DexHigh.MotorGain > DexLow.MotorGain);
        CHECK(DexHigh.MoveSpeedScale >= DexLow.MoveSpeedScale);
        CHECK(DexHigh.AttackSpeedScale >= DexLow.AttackSpeedScale);

        const PhysicalProfile ConLow = profileOf({.Constitution = Value});
        const PhysicalProfile ConHigh = profileOf({.Constitution = Value + 1});
        CHECK(getTotalMassKg(ConHigh) > getTotalMassKg(ConLow));
        CHECK(ConHigh.MaxHp > ConLow.MaxHp);
        CHECK(ConHigh.Poise > ConLow.Poise);
        CHECK(ConHigh.MaxStamina > ConLow.MaxStamina);
        CHECK(ConHigh.StaminaRegen > ConLow.StaminaRegen);
    }
}

TEST_CASE("computeProfile: DEX keeps the O.7 corridor for strike speed", "[stats][balance]") {
    CHECK(profileOf({.Dexterity = 10}).AttackSpeedScale == Approx(1.0f));
    // +-25% at DEX 0 and 20; DEX 1 is the lowest stat in use.
    CHECK(profileOf({.Dexterity = 20}).AttackSpeedScale == Approx(1.25f));
    CHECK(profileOf({.Dexterity = 1}).AttackSpeedScale == Approx(0.775f));
    // Beyond the corridor extreme stats stay sane.
    CHECK(profileOf({.Dexterity = 30}).AttackSpeedScale == Approx(1.25f));
    for (int Value = MinStat; Value <= MaxStat; ++Value) {
        const PhysicalProfile Profile = profileOf({.Dexterity = Value});
        CHECK(Profile.AttackSpeedScale >= 0.75f);
        CHECK(Profile.AttackSpeedScale <= 1.25f);
        CHECK(Profile.MoveSpeedScale >= 0.5f);
        CHECK(Profile.MoveSpeedScale <= 1.5f);
    }
}

TEST_CASE("computeProfile: walking speed follows DEX and is clamped", "[stats][balance]") {
    BalanceTable Table = BalanceTable::getDefaults();
    CHECK(computeProfile({.Dexterity = 10}, {}, Table).MoveSpeedScale == Approx(1.0f));
    CHECK(computeProfile({.Dexterity = 20}, {}, Table).MoveSpeedScale == Approx(1.3f));

    Table.MoveSpeedMax = 1.1f;
    CHECK(computeProfile({.Dexterity = 20}, {}, Table).MoveSpeedScale == Approx(1.1f));
    Table.MoveSpeedMin = 0.95f;
    CHECK(computeProfile({.Dexterity = 1}, {}, Table).MoveSpeedScale == Approx(0.95f));
}

TEST_CASE("computeProfile: armor raises poise and heavier armor raises it more", "[stats][balance]") {
    const BalanceTable Balance = BalanceTable::getDefaults();
    const ItemCatalog Catalog = loadItemCatalog(DataDir / "items");
    const auto Wear = [&](const std::vector<std::string>& Ids) {
        return computeProfile({}, buildLoadout(Ids, Catalog), Balance);
    };

    const PhysicalProfile Naked = profileOf({});
    const PhysicalProfile Leather = Wear({"leather_cap", "leather_vest"});
    const PhysicalProfile Iron = Wear({"iron_helmet", "chainmail"});
    CHECK(Leather.Poise > Naked.Poise);
    CHECK(Iron.Poise > Leather.Poise);

    // Armor needs no stat: with the coefficient at zero the poise comes from CON alone.
    BalanceTable NoArmorBonus = Balance;
    NoArmorBonus.PoisePerArmor = 0.0f;
    const std::vector<std::string> IronIds = {"iron_helmet", "chainmail"};
    CHECK(computeProfile({}, buildLoadout(IronIds, Catalog), NoArmorBonus).Poise == Approx(Naked.Poise));
}

TEST_CASE("computeProfile: the mean armor is weighted by body part mass", "[stats][balance]") {
    const BalanceTable Balance = BalanceTable::getDefaults();
    CHECK(getMeanArmor(profileOf({}), Balance) == Approx(0.0f));

    Loadout Torso;
    Torso.Items.push_back({.Id = "vest", .Slot = EquipmentSlot::Body, .Covers = {BodyPart::Torso}, .Armor = 0.5f});
    Loadout Foot;
    Foot.Items.push_back({.Id = "sock", .Slot = EquipmentSlot::Feet, .Covers = {BodyPart::FootL}, .Armor = 0.5f});
    const float TorsoMean = getMeanArmor(profileOf({}, Torso), Balance);
    const float FootMean = getMeanArmor(profileOf({}, Foot), Balance);
    CHECK(TorsoMean > FootMean);
    CHECK(TorsoMean == Approx(0.5f * 26.0f / 73.4f).epsilon(0.01));
}

TEST_CASE("computeProfile: stacked armor is capped per body part", "[stats][balance]") {
    Loadout Gear;
    Gear.Items.push_back({.Id = "a", .Slot = EquipmentSlot::Head, .Covers = {BodyPart::Head}, .Armor = 0.8f});
    Gear.Items.push_back({.Id = "b", .Slot = EquipmentSlot::Body, .Covers = {BodyPart::Head}, .Armor = 0.8f});
    const PhysicalProfile Profile = profileOf({}, Gear);
    CHECK(Profile.Parts[static_cast<size_t>(BodyPart::Head)].Armor == Approx(0.9f));

    BalanceTable Table = BalanceTable::getDefaults();
    Table.MaxPartArmor = 0.6f;
    CHECK(computeProfile({}, Gear, Table).Parts[static_cast<size_t>(BodyPart::Head)].Armor == Approx(0.6f));
}

TEST_CASE("computeProfile: equipment mass adds to the body part masses", "[stats][balance]") {
    const ResolvedFighter Knight = loadFighter("knight");
    const PhysicalProfile Naked = profileOf(Knight.BaseStats);
    const PhysicalProfile Armored = profileOf(Knight.BaseStats, Knight.Gear);
    float GearKg = 0.0f;
    for (const EquipmentItem& Item : Knight.Gear.Items) GearKg += Item.MassKg;
    CHECK(getTotalMassKg(Armored) - getTotalMassKg(Naked) == Approx(GearKg).margin(1e-3));
}

TEST_CASE("computeProfile: the knight and the rogue differ as built", "[stats][balance][data]") {
    const ResolvedFighter Knight = loadFighter("knight");
    const ResolvedFighter Rogue = loadFighter("rogue");
    const PhysicalProfile K = profileOf(Knight.BaseStats, Knight.Gear);
    const PhysicalProfile R = profileOf(Rogue.BaseStats, Rogue.Gear);

    CHECK(getTotalMassKg(K) > getTotalMassKg(R));
    CHECK(K.Poise > R.Poise);
    CHECK(K.MaxHp > R.MaxHp);
    CHECK(K.MaxStamina > R.MaxStamina);
    CHECK(K.MotorMaxTorque > R.MotorMaxTorque);
    // The rogue is the quick one: walking, strikes and motors.
    CHECK(K.AttackSpeedScale < R.AttackSpeedScale);
    CHECK(K.MoveSpeedScale < R.MoveSpeedScale);
    CHECK(K.MotorGain < R.MotorGain);
}

TEST_CASE("computeProfile: the balance table, not the code, sets the numbers", "[stats][balance]") {
    BalanceTable Table = BalanceTable::getDefaults();
    const PhysicalProfile Before = computeProfile({.Constitution = 14}, {}, Table);
    Table.HpPerCon *= 2.0f;
    Table.PoisePerCon *= 2.0f;
    Table.StaminaPerCon *= 2.0f;
    Table.StaminaRegenPerCon *= 2.0f;
    Table.MassPerCon *= 2.0f;
    const PhysicalProfile After = computeProfile({.Constitution = 14}, {}, Table);
    CHECK(After.MaxHp > Before.MaxHp);
    CHECK(After.Poise > Before.Poise);
    CHECK(After.MaxStamina > Before.MaxStamina);
    CHECK(After.StaminaRegen > Before.StaminaRegen);
    CHECK(getTotalMassKg(After) > getTotalMassKg(Before));
}

TEST_CASE("getTotalMassKg: the base body weighs about 75 kg", "[stats][balance]") {
    CHECK(getTotalMassKg(profileOf({})) == Approx(73.4f).epsilon(0.001));
}

// --- Debug text --------------------------------------------------------------

TEST_CASE("describeProfile: gives the five panel lines with the profile numbers", "[stats][balance]") {
    const ResolvedFighter Knight = loadFighter("knight");
    const BalanceTable Balance = BalanceTable::getDefaults();
    const PhysicalProfile Profile = computeProfile(Knight.BaseStats, Knight.Gear, Balance);
    const auto Lines = describeProfile(Knight.BaseStats, Knight.Gear, Profile, Balance);

    REQUIRE(Lines.size() == 5);
    CHECK(Lines[0].Label == "build");
    CHECK_THAT(Lines[0].Text, ContainsSubstring("STR 14 DEX 8 CON 14"));
    CHECK_THAT(Lines[0].Text, ContainsSubstring("hammer"));
    CHECK(Lines[1].Label == "mass");
    CHECK_THAT(Lines[1].Text, ContainsSubstring(std::format("{:.1f} kg", getTotalMassKg(Profile))));
    CHECK(Lines[2].Label == "motors");
    CHECK(Lines[3].Label == "speed");
    CHECK_THAT(Lines[3].Text, ContainsSubstring(std::format("strike x{:.2f}", Profile.AttackSpeedScale)));
    CHECK(Lines[4].Label == "vitals");
    CHECK_THAT(Lines[4].Text, ContainsSubstring(std::format("poise {:.2f}", Profile.Poise)));

    const auto Unarmed = describeProfile({}, {}, profileOf({}), Balance);
    CHECK_THAT(Unarmed[0].Text, ContainsSubstring("unarmed"));
}
