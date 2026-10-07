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
    CHECK(getEquipmentSlotName(EquipmentSlot::MainHand) == "MainHand");
    CHECK_FALSE(findEquipmentSlot("Helmet").has_value());
    CHECK_FALSE(findEquipmentSlot("head").has_value());
}

TEST_CASE("listBodyPartNames and listEquipmentSlotNames: comma-separated names", "[stats][equipment]") {
    CHECK_THAT(listEquipmentSlotNames(), Equals("Head, Body, Hands, Legs, Feet, MainHand, OffHand"));
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
    CHECK_THROWS_AS(Catalog.addItem(makeItem("anvil", EquipmentSlot::MainHand, {}, 99.0f)),
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

    CHECK(buildLoadout(std::vector<std::string>{}, Catalog).Items.empty());
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
        Catalog.addItem(makeItem(Id, Slot, {BodyPart::Torso}, 9.0f));
        Ids.push_back(std::move(Id));
    }

    CHECK_THROWS_WITH(buildLoadout(Ids, Catalog), Equals("total item mass 63 kg exceeds the limit of 60 kg"));
    Ids.pop_back();
    CHECK_NOTHROW(buildLoadout(Ids, Catalog));
}

TEST_CASE("validateLoadout: checks a loadout built in code", "[stats][validation]") {
    Loadout Gear;
    Gear.Items.push_back(makeItem("sword", EquipmentSlot::MainHand, {BodyPart::ForearmR}));
    CHECK_NOTHROW(validateLoadout(Gear));

    Gear.Items.push_back(makeItem("axe", EquipmentSlot::MainHand, {BodyPart::ForearmL}));
    CHECK_THROWS_WITH(validateLoadout(Gear), Equals("items 'sword' and 'axe' both take the MainHand slot"));

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

TEST_CASE("buildLoadout: hands, two-handed items and changing hands", "[stats][equipment]") {
    ItemCatalog Catalog = makeCatalog();
    EquipmentItem Sword = makeItem("sword", EquipmentSlot::MainHand, {}, 1.2f, 0.0f);
    Sword.MoveSet = "sword";
    Sword.Weapon = WeaponProps{.ReachM = 0.5f};
    Catalog.addItem(Sword);
    EquipmentItem Shield = makeItem("shield", EquipmentSlot::OffHand, {}, 3.0f, 0.3f);
    Shield.MoveSet = "shield";
    Shield.Shield = ShieldProps{.LengthM = 0.5f, .WidthM = 0.4f};
    Catalog.addItem(Shield);
    EquipmentItem Great = makeItem("great", EquipmentSlot::MainHand, {}, 3.0f, 0.0f);
    Great.TwoHanded = true;
    Great.Weapon = WeaponProps{.ReachM = 0.9f};
    Catalog.addItem(Great);
    const auto Build = [&](std::vector<ItemRef> Items) { return buildLoadout(Items, Catalog); };

    const Loadout Armed = Build({{.Id = "sword"}, {.Id = "shield"}});
    CHECK(Armed.findInSlot(EquipmentSlot::MainHand)->Covers == std::vector{BodyPart::ForearmR});
    CHECK(Armed.findInSlot(EquipmentSlot::OffHand)->Covers == std::vector{BodyPart::ForearmL});
    CHECK(Armed.getMoveSet(EquipmentSlot::MainHand) == "sword");
    CHECK(Armed.getMoveSet(EquipmentSlot::OffHand) == "shield");
    CHECK(Armed.findWeapon()->ReachM == 0.5f);

    // A sword in the left hand: it covers the left forearm; the main hand is empty.
    const Loadout Left = Build({{.Id = "sword", .Slot = EquipmentSlot::OffHand}});
    CHECK(Left.findInSlot(EquipmentSlot::OffHand)->Covers == std::vector{BodyPart::ForearmL});
    CHECK(Left.findWeapon() == nullptr);
    CHECK(Left.getMoveSet(EquipmentSlot::OffHand) == "sword");

    const Loadout Both = Build({{.Id = "great"}});
    CHECK(Both.findInSlot(EquipmentSlot::OffHand)->Id == "great");
    CHECK(Both.findInSlot(EquipmentSlot::MainHand)->Covers == std::vector{BodyPart::ForearmR, BodyPart::ForearmL});
    CHECK(Both.getMoveSet(EquipmentSlot::OffHand).empty());

    CHECK_THROWS_WITH(Build({{.Id = "great"}, {.Id = "shield"}}),
                      Equals("items 'great' and 'shield' both take the OffHand slot"));
    CHECK_THROWS_WITH(Build({{.Id = "great", .Slot = EquipmentSlot::OffHand}}),
                      Equals("item 'great' cannot be put into the OffHand slot"));
    CHECK_THROWS_WITH(Build({{.Id = "vest", .Slot = EquipmentSlot::OffHand}}),
                      Equals("item 'vest' cannot be put into the OffHand slot"));
}

TEST_CASE("validateItem: components belong to items held in a hand", "[stats][validation]") {
    EquipmentItem Vest = makeItem("vest", EquipmentSlot::Body, {BodyPart::Torso});
    Vest.MoveSet = "vest";
    CHECK_THROWS_WITH(validateItem(Vest), ContainsSubstring("is not held in a hand"));

    EquipmentItem Shield = makeItem("shield", EquipmentSlot::OffHand, {});
    Shield.Shield = ShieldProps{.LengthM = 0.0f, .WidthM = 0.4f};
    CHECK_THROWS_WITH(validateItem(Shield), Equals("item 'shield': shield length 0 m is out of (0, 1.2] m"));
    Shield.Shield->LengthM = 0.5f;
    Shield.TwoHanded = true;
    CHECK_THROWS_WITH(validateItem(Shield), ContainsSubstring("two-handed but its slot is not MainHand"));

    EquipmentItem Sword = makeItem("sword", EquipmentSlot::MainHand, {});
    Sword.Weapon = WeaponProps{.ReachM = 0.5f, .AngleDeg = 200.0f};
    CHECK_THROWS_WITH(validateItem(Sword), ContainsSubstring("weapon angle 200"));
}
