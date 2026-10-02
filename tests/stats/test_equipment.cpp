#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/body.hpp"
#include "stats/equipment.hpp"
#include "stats/stats.hpp"
#include "stats/validation.hpp"

using namespace fighter;
using namespace fighter::stats;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

namespace {

EquipmentItem makeItem(std::string Id, EquipmentSlot Slot, std::vector<BodyPart> Covers, float MassKg = 1.0f,
                       float Armor = 0.1f) {
    return {.Id = std::move(Id), .Name = {}, .Slot = Slot, .Covers = std::move(Covers), .MassKg = MassKg,
            .Armor = Armor};
}

ItemCatalog makeCatalog() {
    ItemCatalog Catalog;
    Catalog.addItem(makeItem("cap", EquipmentSlot::Head, {BodyPart::Head}, 0.8f, 0.1f));
    Catalog.addItem(makeItem("helmet", EquipmentSlot::Head, {BodyPart::Head}, 2.5f, 0.3f));
    Catalog.addItem(makeItem("vest", EquipmentSlot::Body, {BodyPart::Torso}, 3.0f, 0.15f));
    Catalog.addItem(makeItem("boots", EquipmentSlot::Feet, {BodyPart::FootL, BodyPart::FootR}, 1.6f, 0.1f));
    return Catalog;
}

} // namespace

TEST_CASE("findBodyPart: every body part is found by its own name", "[stats][equipment]") {
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        CHECK(findBodyPart(getBodyPartName(Part)) == Part);
    }
    CHECK_FALSE(findBodyPart("Hed").has_value());
    CHECK_FALSE(findBodyPart("head").has_value());
    CHECK_FALSE(findBodyPart("").has_value());
    CHECK_FALSE(findBodyPart("?").has_value());
}

TEST_CASE("findEquipmentSlot: every slot is found by its own name", "[stats][equipment]") {
    for (EquipmentSlot Slot : EquipmentSlots) CHECK(findEquipmentSlot(getEquipmentSlotName(Slot)) == Slot);
    CHECK(getEquipmentSlotName(EquipmentSlot::Weapon) == "Weapon");
    CHECK_FALSE(findEquipmentSlot("Helmet").has_value());
    CHECK_FALSE(findEquipmentSlot("head").has_value());
}

TEST_CASE("listBodyPartNames and listEquipmentSlotNames: comma-separated names", "[stats][equipment]") {
    CHECK_THAT(listEquipmentSlotNames(), Equals("Head, Body, Hands, Legs, Feet, Weapon"));
    CHECK_THAT(listBodyPartNames(), ContainsSubstring("Head, Torso, Pelvis"));
    CHECK_THAT(listBodyPartNames(), ContainsSubstring("FootR"));
}

TEST_CASE("validateStats: accepts the range bounds and rejects values outside", "[stats][validation]") {
    CHECK_NOTHROW(validateStats({}));
    CHECK_NOTHROW(validateStats({.Strength = MinStatValue, .Dexterity = MaxStatValue, .Constitution = 10}));

    CHECK_THROWS_WITH(validateStats({.Strength = 0}), Equals("strength 0 is out of range [1, 30]"));
    CHECK_THROWS_WITH(validateStats({.Dexterity = 31}), Equals("dexterity 31 is out of range [1, 30]"));
    CHECK_THROWS_WITH(validateStats({.Constitution = -5}), Equals("constitution -5 is out of range [1, 30]"));
    CHECK_THROWS_AS(validateStats({.Strength = 100}), DataError);
}

TEST_CASE("validateItem: accepts a valid item", "[stats][validation]") {
    CHECK_NOTHROW(validateItem(makeItem("helmet", EquipmentSlot::Head, {BodyPart::Head})));
    CHECK_NOTHROW(validateItem(makeItem("ring", EquipmentSlot::Hands, {BodyPart::ForearmL}, 0.0f, 0.0f)));
    CHECK_NOTHROW(validateItem(
        makeItem("plate", EquipmentSlot::Body, {BodyPart::Torso}, MaxItemMassKg, MaxItemArmor)));
}

TEST_CASE("validateItem: rejects an empty id and empty or repeated covers", "[stats][validation]") {
    CHECK_THROWS_WITH(validateItem(makeItem("", EquipmentSlot::Head, {BodyPart::Head})),
                      Equals("item id is empty"));
    CHECK_THROWS_WITH(validateItem(makeItem("helmet", EquipmentSlot::Head, {})),
                      Equals("item 'helmet' covers no body parts"));
    CHECK_THROWS_WITH(validateItem(makeItem("helmet", EquipmentSlot::Head, {BodyPart::Head, BodyPart::Head})),
                      Equals("item 'helmet' covers Head twice"));
    CHECK_THROWS_WITH(validateItem(makeItem("helmet", EquipmentSlot::Head, {BodyPart::Count})),
                      ContainsSubstring("covers an unknown body part"));
}

TEST_CASE("validateItem: rejects mass and armor out of range", "[stats][validation]") {
    const auto MakeHelmet = [](float MassKg, float Armor) {
        return makeItem("helmet", EquipmentSlot::Head, {BodyPart::Head}, MassKg, Armor);
    };
    CHECK_THROWS_WITH(validateItem(MakeHelmet(-1.0f, 0.1f)),
                      Equals("item 'helmet': mass -1 kg is out of range [0, 30] kg"));
    CHECK_THROWS_WITH(validateItem(MakeHelmet(31.0f, 0.1f)),
                      Equals("item 'helmet': mass 31 kg is out of range [0, 30] kg"));
    CHECK_THROWS_WITH(validateItem(MakeHelmet(1.0f, 1.5f)),
                      Equals("item 'helmet': armor 1.5 is out of range [0, 1]"));
    CHECK_THROWS_WITH(validateItem(MakeHelmet(1.0f, -0.1f)), ContainsSubstring("armor -0.1 is out of range"));
    CHECK_THROWS_AS(validateItem(MakeHelmet(std::numeric_limits<float>::quiet_NaN(), 0.1f)), DataError);
    CHECK_THROWS_AS(validateItem(MakeHelmet(1.0f, std::numeric_limits<float>::infinity())), DataError);
}

TEST_CASE("ItemCatalog: finds added items by id", "[stats][equipment]") {
    const ItemCatalog Catalog = makeCatalog();
    REQUIRE(Catalog.getSize() == 4);

    const EquipmentItem* Helmet = Catalog.findItem("helmet");
    REQUIRE(Helmet != nullptr);
    CHECK(Helmet->Id == "helmet");
    CHECK(Helmet->Slot == EquipmentSlot::Head);
    CHECK(Helmet->MassKg == 2.5f);

    CHECK(Catalog.findItem("sword") == nullptr);
    CHECK(Catalog.findItem("") == nullptr);
    CHECK(Catalog.getItems().front().Id == "cap");
    CHECK(Catalog.getItems().back().Id == "boots");
}

TEST_CASE("ItemCatalog: rejects duplicate ids and invalid items", "[stats][equipment]") {
    ItemCatalog Catalog = makeCatalog();
    CHECK_THROWS_WITH(Catalog.addItem(makeItem("helmet", EquipmentSlot::Head, {BodyPart::Head})),
                      Equals("duplicate item id 'helmet'"));
    CHECK_THROWS_AS(Catalog.addItem(makeItem("anvil", EquipmentSlot::Weapon, {BodyPart::ForearmR}, 99.0f)),
                    DataError);
    CHECK(Catalog.getSize() == 4);
    CHECK(Catalog.findItem("anvil") == nullptr);
}

TEST_CASE("buildLoadout: copies the listed items in order", "[stats][equipment]") {
    const ItemCatalog Catalog = makeCatalog();
    const std::vector<std::string> Ids = {"boots", "helmet", "vest"};
    const Loadout Gear = buildLoadout(Ids, Catalog);

    REQUIRE(Gear.Items.size() == 3);
    CHECK(Gear.Items[0].Id == "boots");
    CHECK(Gear.Items[1].Id == "helmet");
    CHECK(Gear.Items[2].Id == "vest");
    CHECK(Gear.Items[0].Covers == std::vector<BodyPart>{BodyPart::FootL, BodyPart::FootR});

    CHECK(buildLoadout({}, Catalog).Items.empty());
}

TEST_CASE("buildLoadout: rejects unknown ids, repeats and slot conflicts", "[stats][equipment]") {
    const ItemCatalog Catalog = makeCatalog();
    const auto Build = [&](std::vector<std::string> Ids) { return buildLoadout(Ids, Catalog); };

    CHECK_THROWS_WITH(Build({"helmet", "sword"}), Equals("unknown item id 'sword'"));
    CHECK_THROWS_WITH(Build({"vest", "vest"}), Equals("item 'vest' is listed twice"));
    CHECK_THROWS_WITH(Build({"cap", "vest", "helmet"}), Equals("items 'cap' and 'helmet' both take the Head slot"));
}

TEST_CASE("buildLoadout: rejects a loadout heavier than the limit", "[stats][equipment]") {
    ItemCatalog Catalog;
    std::vector<std::string> Ids;
    for (EquipmentSlot Slot : EquipmentSlots) {
        std::string Id = "heavy_" + std::string(getEquipmentSlotName(Slot));
        Catalog.addItem(makeItem(Id, Slot, {BodyPart::Torso}, 11.0f));
        Ids.push_back(std::move(Id));
    }

    CHECK_THROWS_WITH(buildLoadout(Ids, Catalog), Equals("total item mass 66 kg exceeds the limit of 60 kg"));
    Ids.pop_back();
    CHECK_NOTHROW(buildLoadout(Ids, Catalog));
}

TEST_CASE("validateLoadout: checks a loadout built in code", "[stats][validation]") {
    Loadout Gear;
    Gear.Items.push_back(makeItem("sword", EquipmentSlot::Weapon, {BodyPart::ForearmR}));
    CHECK_NOTHROW(validateLoadout(Gear));

    Gear.Items.push_back(makeItem("axe", EquipmentSlot::Weapon, {BodyPart::ForearmL}));
    CHECK_THROWS_WITH(validateLoadout(Gear), Equals("items 'sword' and 'axe' both take the Weapon slot"));

    Gear.Items.back() = makeItem("bad", EquipmentSlot::Head, {BodyPart::Head}, -1.0f);
    CHECK_THROWS_WITH(validateLoadout(Gear), ContainsSubstring("item 'bad': mass -1 kg"));
}

TEST_CASE("withErrorContext: prefixes DataError messages and passes results through", "[stats][validation]") {
    CHECK(withErrorContext("ctx", [] { return 42; }) == 42);
    const auto Fail = [] { validateStats({.Strength = 0}); };
    CHECK_THROWS_WITH(withErrorContext("file.json", Fail), Equals("file.json: strength 0 is out of range [1, 30]"));
    CHECK_THROWS_WITH(withErrorContext("outer", [&] { withErrorContext("inner", Fail); }),
                      Equals("outer: inner: strength 0 is out of range [1, 30]"));
}
