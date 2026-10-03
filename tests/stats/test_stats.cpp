#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::stats;

namespace {
float getTotalMass(const PhysicalProfile& Profile) {
    float Sum = 0.0f;
    for (const PartParams& Part : Profile.Parts) Sum += Part.MassKg;
    return Sum;
}
} // namespace

TEST_CASE("computeProfile: base fighter weighs about 75 kg", "[stats]") {
    const auto Profile = computeProfile({}, {}, BalanceTable::getDefaults());
    CHECK(getTotalMass(Profile) > 70.0f);
    CHECK(getTotalMass(Profile) < 80.0f);
    CHECK(Profile.MaxHp > 0.0f);
}

TEST_CASE("computeProfile: stats move parameters in the right direction", "[stats]") {
    const auto Balance = BalanceTable::getDefaults();
    const auto Base = computeProfile({}, {}, Balance);

    CHECK(computeProfile({.Strength = 15}, {}, Balance).MotorMaxTorque > Base.MotorMaxTorque);
    const auto Nimble = computeProfile({.Dexterity = 15}, {}, Balance);
    CHECK(Nimble.MotorGain > Base.MotorGain);
    CHECK(Base.MoveSpeedScale == 1.0f);
    CHECK(Nimble.MoveSpeedScale > Base.MoveSpeedScale);
    CHECK(Base.AttackSpeedScale == 1.0f);
    CHECK(Nimble.AttackSpeedScale > Base.AttackSpeedScale);
    const auto Tough = computeProfile({.Constitution = 15}, {}, Balance);
    CHECK(getTotalMass(Tough) > getTotalMass(Base));
    CHECK(Tough.MaxHp > Base.MaxHp);
    CHECK(Tough.Poise > Base.Poise);
    CHECK(Base.MaxStamina > 0.0f);
    CHECK(Tough.MaxStamina > Base.MaxStamina);
    CHECK(Base.StaminaRegen > 0.0f);
    CHECK(Tough.StaminaRegen > Base.StaminaRegen);
}

TEST_CASE("computeProfile: DEX moves strike speed by at most a quarter", "[stats]") {
    // O.7: DEX shifts strike speed by +-25% over the whole stat range in use.
    const auto Balance = BalanceTable::getDefaults();
    CHECK(computeProfile({.Dexterity = 20}, {}, Balance).AttackSpeedScale <= 1.25f + 1e-5f);
    CHECK(computeProfile({.Dexterity = 1}, {}, Balance).AttackSpeedScale >= 0.75f - 1e-5f);
}

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
