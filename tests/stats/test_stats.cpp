#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::stats;
using Catch::Approx;

TEST_CASE("computeProfile: equipment adds mass and armor to its body parts", "[stats]") {
    Loadout Gear;
    Gear.Items.push_back({.Id = "helmet", .Slot = EquipmentSlot::Head,
                          .Covers = {BodyPart::Head}, .MassKg = 2.0f, .Armor = 0.3f});
    const auto Balance = BalanceTable::getDefaults();
    const auto Base = computeProfile({}, {}, Balance);
    const auto Armored = computeProfile({}, Gear, Balance);

    const auto Head = static_cast<size_t>(BodyPart::Head);
    const auto Torso = static_cast<size_t>(BodyPart::Torso);
    CHECK(Armored.Parts[Head].MassKg == Base.Parts[Head].MassKg + 2.0f);
    CHECK(Armored.Parts[Head].Armor == 0.3f);
    CHECK(Armored.Parts[Torso].MassKg == Base.Parts[Torso].MassKg);
}

TEST_CASE("computeProfile: a shield adds poise only in the main hand", "[stats]") {
    // Decision 2026-10-08: a shield in the main hand gives stability, one in
    // the off hand only guards. No armor and no mass, so poise differs by
    // the shield's bonus alone.
    const auto Balance = BalanceTable::getDefaults();
    EquipmentItem Shield{.Id = "shield", .Slot = EquipmentSlot::OffHand};
    Shield.Shield = ShieldProps{.LengthM = 0.4f, .WidthM = 0.4f, .PoiseBonus = 0.5f};
    const Loadout OffHand{.Items = {Shield}};
    Shield.Slot = EquipmentSlot::MainHand;
    const Loadout MainHand{.Items = {Shield}};

    const float Base = computeProfile({}, {}, Balance).Poise;
    CHECK(OffHand.findShield(EquipmentSlot::OffHand) != nullptr);
    CHECK(OffHand.findShield(EquipmentSlot::MainHand) == nullptr);
    CHECK(computeProfile({}, OffHand, Balance).Poise == Base);
    CHECK(computeProfile({}, MainHand, Balance).Poise == Approx(Base * 1.5f));
}
