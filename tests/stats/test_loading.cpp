#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/body.hpp"
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

constexpr std::string_view ValidItems = R"({"items": [
    {"id": "helmet", "name": "Iron helmet", "slot": "Head", "covers": ["Head"], "mass_kg": 2.5, "armor": 0.3},
    {"id": "greaves", "slot": "Legs", "covers": ["ThighL", "ShinL"], "mass_kg": 4, "armor": 0.2}
]})";

/// Wraps one item object into an item file.
std::string makeItemFile(std::string_view Item) { return std::format(R"({{"items": [{}]}})", Item); }

/// A temporary directory that is removed with everything in it.
class TempDirectory {
public:
    TempDirectory() {
        const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        Path = std::filesystem::temp_directory_path() / std::format("fighter_stats_test_{}", Stamp);
        std::filesystem::create_directories(Path);
    }
    ~TempDirectory() {
        std::error_code Ignored;
        std::filesystem::remove_all(Path, Ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    std::filesystem::path writeFile(std::string_view Name, std::string_view Contents) const {
        const auto FilePath = Path / Name;
        std::ofstream(FilePath, std::ios::binary) << Contents;
        return FilePath;
    }

    const std::filesystem::path& getPath() const { return Path; }

private:
    std::filesystem::path Path;
};

float getTotalMass(const PhysicalProfile& Profile) {
    float Sum = 0.0f;
    for (const PartParams& Part : Profile.Parts) Sum += Part.MassKg;
    return Sum;
}

float getTotalItemMass(const Loadout& Gear) {
    float Sum = 0.0f;
    for (const EquipmentItem& Item : Gear.Items) Sum += Item.MassKg;
    return Sum;
}

} // namespace

// --- Stats -------------------------------------------------------------------

TEST_CASE("parseStats: reads all three stats", "[stats][loading]") {
    const Stats Parsed = parseStats(R"({"strength": 14, "dexterity": 8, "constitution": 30})");
    CHECK(Parsed.Strength == 14);
    CHECK(Parsed.Dexterity == 8);
    CHECK(Parsed.Constitution == 30);
}

TEST_CASE("parseStats: reports the source, field and value", "[stats][loading]") {
    CHECK_THROWS_WITH(parseStats(R"({"strength": 42, "dexterity": 8, "constitution": 10})", "me.json"),
                      Equals("me.json: strength 42 is out of range [1, 30]"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 10, "dexterity": 8})"),
                      Equals("<inline>: missing field 'constitution'"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": "high", "dexterity": 8, "constitution": 10})"),
                      Equals("<inline>: field 'strength': expected an integer, got \"high\""));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 12.5, "dexterity": 8, "constitution": 10})"),
                      Equals("<inline>: field 'strength': expected an integer, got 12.5"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 10, "dexterity": 99999999999, "constitution": 10})"),
                      Equals("<inline>: field 'dexterity': 99999999999 does not fit into an int"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 10, "dexterity": -99999999999, "constitution": 10})"),
                      ContainsSubstring("does not fit into an int"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 10, "dexterity": 8, "constitution": 10, "luck": 3})"),
                      Equals("<inline>: unknown field 'luck' (expected one of: strength, dexterity, constitution)"));
    CHECK_THROWS_WITH(parseStats("[1, 2, 3]"), Equals("<inline>: expected an object, got array"));
    CHECK_THROWS_WITH(parseStats(R"({"strength": 10,)"), StartsWith("<inline>: invalid JSON: "));
    CHECK_THROWS_AS(parseStats(""), DataError);
}

// --- Item catalog ------------------------------------------------------------

TEST_CASE("parseItemCatalog: reads items, the name defaults to the id", "[stats][loading]") {
    const ItemCatalog Catalog = parseItemCatalog(ValidItems);
    REQUIRE(Catalog.getSize() == 2);

    const EquipmentItem* Helmet = Catalog.findItem("helmet");
    REQUIRE(Helmet != nullptr);
    CHECK(Helmet->Name == "Iron helmet");
    CHECK(Helmet->Slot == EquipmentSlot::Head);
    CHECK(Helmet->Covers == std::vector<BodyPart>{BodyPart::Head});
    CHECK(Helmet->MassKg == 2.5f);
    CHECK(Helmet->Armor == 0.3f);

    const EquipmentItem* Greaves = Catalog.findItem("greaves");
    REQUIRE(Greaves != nullptr);
    CHECK(Greaves->Name == "greaves");
    CHECK(Greaves->Covers == std::vector<BodyPart>{BodyPart::ThighL, BodyPart::ShinL});
    CHECK(Greaves->MassKg == 4.0f);

    CHECK(parseItemCatalog(R"({"items": []})").getSize() == 0);
}

TEST_CASE("parseItemCatalog: rejects a malformed file structure", "[stats][loading]") {
    CHECK_THROWS_WITH(parseItemCatalog("[]", "a.json"), Equals("a.json: expected an object, got array"));
    CHECK_THROWS_WITH(parseItemCatalog("{}", "a.json"), Equals("a.json: missing field 'items'"));
    CHECK_THROWS_WITH(parseItemCatalog(R"({"items": {}})", "a.json"),
                      Equals("a.json: field 'items': expected an array, got object"));
    CHECK_THROWS_WITH(parseItemCatalog(R"({"items": [], "extra": 1})", "a.json"),
                      Equals("a.json: unknown field 'extra' (expected one of: items)"));
    CHECK_THROWS_WITH(parseItemCatalog(R"({"items": [42]})", "a.json"),
                      Equals("a.json: items[0]: expected an object, got number"));
    CHECK_THROWS_WITH(parseItemCatalog("{\"items\": [", "a.json"), StartsWith("a.json: invalid JSON: "));
}

TEST_CASE("parseItemCatalog: names the item index and the bad field", "[stats][loading]") {
    const auto Parse = [](std::string_view Item) { return parseItemCatalog(makeItemFile(Item), "a.json"); };

    CHECK_THROWS_WITH(Parse(R"({"slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: missing field 'id'"));
    CHECK_THROWS_WITH(Parse(R"({"id": "", "slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: field 'id': must not be empty"));
    CHECK_THROWS_WITH(Parse(R"({"id": 7, "slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: field 'id': expected a string, got number"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Hat", "covers": ["Head"], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: field 'slot': unknown slot 'Hat' "
                             "(expected one of: Head, Body, Hands, Legs, Feet, Weapon)"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": "Head", "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: field 'covers': expected an array, got string"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head", 3], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: covers[1]: expected a string, got number"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head", "Hed"], "mass_kg": 1,
                                "armor": 0})"),
                      StartsWith("a.json: items[0]: covers[1]: unknown body part 'Hed' (expected one of: Head, "));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": "1", "armor": 0})"),
                      Equals("a.json: items[0]: field 'mass_kg': expected a number, got string"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": 1})"),
                      Equals("a.json: items[0]: missing field 'armor'"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mas_kg": 1, "armor": 0})"),
                      StartsWith("a.json: items[0]: unknown field 'mas_kg' (expected one of: id, name, slot, "));
}

TEST_CASE("parseItemCatalog: applies item validation", "[stats][loading]") {
    const auto Parse = [](std::string_view Item) { return parseItemCatalog(makeItemFile(Item), "a.json"); };

    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 1.5})"),
                      Equals("a.json: items[0]: item 'cap': armor 1.5 is out of range [0, 1]"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": -2, "armor": 0})"),
                      Equals("a.json: items[0]: item 'cap': mass -2 kg is out of range [0, 30] kg"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": [], "mass_kg": 1, "armor": 0})"),
                      Equals("a.json: items[0]: item 'cap' covers no body parts"));
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head", "Head"], "mass_kg": 1,
                                "armor": 0})"),
                      Equals("a.json: items[0]: item 'cap' covers Head twice"));

    const std::string_view Sword = R"({"id": "sword", "slot": "Weapon", "covers": ["ForearmR"], "mass_kg": 1,
        "armor": 0, "weapon": {"class": "sword", "reach_m": 0.5, "speed_scale": 1, "power_scale": 1.2}})";
    CHECK(Parse(Sword).findItem("sword")->Weapon->Class == "sword");
    CHECK_THROWS_WITH(Parse(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 0,
        "weapon": {"class": "sword", "reach_m": 0.5, "speed_scale": 1, "power_scale": 1}})"),
                      Equals("a.json: items[0]: item 'cap' has weapon properties but is not in the Weapon slot"));
    CHECK_THROWS_WITH(Parse(R"({"id": "sword", "slot": "Weapon", "covers": ["ForearmR"], "mass_kg": 1, "armor": 0,
        "weapon": {"class": "", "reach_m": 0.5, "speed_scale": 1, "power_scale": 1}})"),
                      Equals("a.json: items[0]: weapon: field 'class': must not be empty"));
    CHECK_THROWS_WITH(Parse(R"({"id": "sword", "slot": "Weapon", "covers": ["ForearmR"], "mass_kg": 1, "armor": 0,
        "weapon": {"class": "sword", "reach_m": 0.5, "speed_scale": 0, "power_scale": 1}})"),
                      Equals("a.json: items[0]: weapon 'sword': speed scale 0 is out of [0.25, 4]"));
    CHECK_THROWS_WITH(Parse(R"({"id": "sword", "slot": "Weapon", "covers": ["ForearmR"], "mass_kg": 1, "armor": 0,
        "weapon": {"class": "sword", "reach_m": 0.5, "speed_scale": 1}})"),
                      Equals("a.json: items[0]: weapon: missing field 'power_scale'"));

    const std::string Duplicate = R"({"items": [
        {"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": 1, "armor": 0},
        {"id": "cap", "slot": "Body", "covers": ["Torso"], "mass_kg": 1, "armor": 0}
    ]})";
    CHECK_THROWS_WITH(parseItemCatalog(Duplicate, "a.json"), Equals("a.json: items[1]: duplicate item id 'cap'"));
}

// --- Fighter sheets ----------------------------------------------------------

TEST_CASE("parseFighterSheet: reads the name, stats and item ids", "[stats][loading]") {
    const FighterSheet Sheet = parseFighterSheet(R"({
        "name": "Tester",
        "stats": {"strength": 12, "dexterity": 11, "constitution": 13},
        "items": ["helmet", "greaves"]
    })");
    CHECK(Sheet.Name == "Tester");
    CHECK(Sheet.BaseStats.Strength == 12);
    CHECK(Sheet.BaseStats.Dexterity == 11);
    CHECK(Sheet.BaseStats.Constitution == 13);
    CHECK(Sheet.ItemIds == std::vector<std::string>{"helmet", "greaves"});
}

TEST_CASE("parseFighterSheet: reports bad fields", "[stats][loading]") {
    const auto Parse = [](std::string_view Text) { return parseFighterSheet(Text, "f.json"); };
    constexpr std::string_view GoodStats = R"("stats": {"strength": 10, "dexterity": 10, "constitution": 10})";

    CHECK_THROWS_WITH(Parse(std::format(R"({{{}, "items": []}})", GoodStats)),
                      Equals("f.json: missing field 'name'"));
    CHECK_THROWS_WITH(Parse(std::format(R"({{"name": "X", {}}})", GoodStats)),
                      Equals("f.json: missing field 'items'"));
    CHECK_THROWS_WITH(Parse(R"({"name": "X", "items": []})"), Equals("f.json: missing field 'stats'"));
    CHECK_THROWS_WITH(Parse(std::format(R"({{"name": "X", {}, "items": ["helmet", ""]}})", GoodStats)),
                      Equals("f.json: items[1]: must not be empty"));
    CHECK_THROWS_WITH(Parse(std::format(R"({{"name": "X", {}, "items": [1]}})", GoodStats)),
                      Equals("f.json: items[0]: expected a string, got number"));
    CHECK_THROWS_WITH(Parse(std::format(R"({{"name": "X", {}, "items": [], "rig": "humanoid"}})", GoodStats)),
                      Equals("f.json: unknown field 'rig' (expected one of: name, stats, items)"));
    CHECK_THROWS_WITH(
        Parse(R"({"name": "X", "stats": {"strength": 0, "dexterity": 10, "constitution": 10}, "items": []})"),
        Equals("f.json: stats: strength 0 is out of range [1, 30]"));
    CHECK_THROWS_WITH(Parse(R"({"name": "X", "stats": {"strength": 10}, "items": []})"),
                      Equals("f.json: stats: missing field 'dexterity'"));
    CHECK_THROWS_WITH(Parse(R"({"name": "X", "stats": 10, "items": []})"),
                      Equals("f.json: stats: expected an object, got number"));
}

TEST_CASE("resolveFighterSheet: builds stats and loadout from the catalog", "[stats][loading]") {
    const ItemCatalog Catalog = parseItemCatalog(ValidItems);
    const FighterSheet Sheet{.Name = "Tester", .BaseStats = {.Strength = 12}, .ItemIds = {"greaves", "helmet"}};

    const ResolvedFighter Fighter = resolveFighterSheet(Sheet, Catalog);
    CHECK(Fighter.Name == "Tester");
    CHECK(Fighter.BaseStats.Strength == 12);
    REQUIRE(Fighter.Gear.Items.size() == 2);
    CHECK(Fighter.Gear.Items[0].Id == "greaves");
    CHECK(Fighter.Gear.Items[1].Id == "helmet");
}

TEST_CASE("resolveFighterSheet: errors start with the fighter name", "[stats][loading]") {
    const ItemCatalog Catalog = parseItemCatalog(ValidItems);
    const auto Resolve = [&](Stats BaseStats, std::vector<std::string> ItemIds) {
        return resolveFighterSheet({.Name = "Tester", .BaseStats = BaseStats, .ItemIds = std::move(ItemIds)},
                                   Catalog);
    };

    CHECK_THROWS_WITH(Resolve({}, {"helmet", "sword"}), Equals("fighter 'Tester': unknown item id 'sword'"));
    CHECK_THROWS_WITH(Resolve({}, {"helmet", "helmet"}), Equals("fighter 'Tester': item 'helmet' is listed twice"));
    CHECK_THROWS_WITH(Resolve({.Dexterity = 31}, {}),
                      Equals("fighter 'Tester': dexterity 31 is out of range [1, 30]"));
}

// --- Files -------------------------------------------------------------------

TEST_CASE("loadItemCatalog: loads every sample item file in data/items", "[stats][loading][data]") {
    const ItemCatalog Catalog = loadItemCatalog(DataDir / "items");
    CHECK(Catalog.getSize() >= 6);

    // At least one item for every slot.
    for (EquipmentSlot Slot : EquipmentSlots) {
        bool Found = false;
        for (const EquipmentItem& Item : Catalog.getItems()) Found = Found || Item.Slot == Slot;
        CHECK(Found);
    }
    for (const EquipmentItem& Item : Catalog.getItems()) CHECK_FALSE(Item.Name.empty());

    // A single file works too.
    const ItemCatalog Weapons = loadItemCatalog(DataDir / "items" / "weapons.json");
    CHECK(Weapons.getSize() > 0);
    for (const EquipmentItem& Item : Weapons.getItems()) {
        CHECK(Item.Slot == EquipmentSlot::Weapon);
        CHECK(Item.Weapon.has_value());
    }
}

TEST_CASE("loadFighterSheet: the sample fighters resolve against the sample items", "[stats][loading][data]") {
    const ItemCatalog Catalog = loadItemCatalog(DataDir / "items");
    const ResolvedFighter Knight = resolveFighterSheet(loadFighterSheet(DataDir / "fighters" / "knight.json"), Catalog);
    const ResolvedFighter Rogue = resolveFighterSheet(loadFighterSheet(DataDir / "fighters" / "rogue.json"), Catalog);

    CHECK(Knight.Name == "Knight");
    CHECK(Rogue.Name == "Rogue");
    CHECK(Knight.Gear.Items.size() == EquipmentSlots.size());
    CHECK(Rogue.Gear.Items.size() == EquipmentSlots.size());

    // Different builds: the knight is strong and heavy, the rogue is quick and light.
    CHECK(Knight.BaseStats.Strength > Rogue.BaseStats.Strength);
    CHECK(Rogue.BaseStats.Dexterity > Knight.BaseStats.Dexterity);
    CHECK(getTotalItemMass(Knight.Gear) > 2.0f * getTotalItemMass(Rogue.Gear));
}

TEST_CASE("computeProfile: a resolved loadout adds its item masses", "[stats][loading][data]") {
    const ItemCatalog Catalog = loadItemCatalog(DataDir / "items");
    const ResolvedFighter Knight = resolveFighterSheet(loadFighterSheet(DataDir / "fighters" / "knight.json"), Catalog);
    const BalanceTable Balance = BalanceTable::getDefaults();

    const PhysicalProfile Naked = computeProfile(Knight.BaseStats, {}, Balance);
    const PhysicalProfile Armored = computeProfile(Knight.BaseStats, Knight.Gear, Balance);
    CHECK(getTotalMass(Armored) - getTotalMass(Naked) == Approx(getTotalItemMass(Knight.Gear)).margin(1e-3));

    const auto Head = static_cast<size_t>(BodyPart::Head);
    const EquipmentItem* Helmet = Catalog.findItem("iron_helmet");
    REQUIRE(Helmet != nullptr);
    CHECK(Armored.Parts[Head].MassKg == Approx(Naked.Parts[Head].MassKg + Helmet->MassKg));
    CHECK(Armored.Parts[Head].Armor == Approx(Helmet->Armor));

    REQUIRE(Knight.Gear.findWeapon() != nullptr);
    CHECK(Knight.Gear.findWeapon()->Class == "hammer");
    CHECK(Loadout{}.findWeapon() == nullptr);
}

TEST_CASE("load functions: errors name the file", "[stats][loading]") {
    const TempDirectory Temp;
    const std::string Missing = (Temp.getPath() / "missing.json").string();

    CHECK_THROWS_WITH(loadFighterSheet(Missing), Equals(Missing + ": cannot open the file"));
    CHECK_THROWS_WITH(loadItemCatalog(Missing), Equals(Missing + ": cannot open the file"));

    const auto BadSheet = Temp.writeFile("bad_sheet.json", R"({"name": "X", "stats": {}, "items": []})");
    CHECK_THROWS_WITH(loadFighterSheet(BadSheet),
                      Equals(BadSheet.string() + ": stats: missing field 'strength'"));

    const auto BadItems = Temp.writeFile("bad_items.json", "{ not json");
    CHECK_THROWS_WITH(loadItemCatalog(BadItems), StartsWith(BadItems.string() + ": invalid JSON: "));
}

TEST_CASE("loadItemCatalog: a directory merges files and rejects ids repeated across files",
          "[stats][loading]") {
    const TempDirectory Temp;
    CHECK_THROWS_WITH(loadItemCatalog(Temp.getPath()),
                      Equals(Temp.getPath().string() + ": no .json item files in the directory"));

    Temp.writeFile("a.json", makeItemFile(R"({"id": "cap", "slot": "Head", "covers": ["Head"], "mass_kg": 1,
                                              "armor": 0})"));
    Temp.writeFile("b.json", makeItemFile(R"({"id": "vest", "slot": "Body", "covers": ["Torso"], "mass_kg": 2,
                                              "armor": 0.1})"));
    Temp.writeFile("notes.txt", "not an item file");
    const ItemCatalog Catalog = loadItemCatalog(Temp.getPath());
    CHECK(Catalog.getSize() == 2);
    CHECK(Catalog.getItems()[0].Id == "cap");
    CHECK(Catalog.getItems()[1].Id == "vest");

    const auto Clash = Temp.writeFile("c.json", makeItemFile(R"({"id": "cap", "slot": "Head", "covers": ["Head"],
                                                                 "mass_kg": 1, "armor": 0})"));
    CHECK_THROWS_WITH(loadItemCatalog(Temp.getPath()),
                      Equals(Clash.string() + ": items[0]: duplicate item id 'cap'"));
}
