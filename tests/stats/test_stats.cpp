#include <catch2/catch_test_macros.hpp>

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
    CHECK(computeProfile({.Dexterity = 15}, {}, Balance).MotorGain > Base.MotorGain);
    const auto Tough = computeProfile({.Constitution = 15}, {}, Balance);
    CHECK(getTotalMass(Tough) > getTotalMass(Base));
    CHECK(Tough.MaxHp > Base.MaxHp);
}

TEST_CASE("computeProfile: equipment adds mass and armor to its body parts", "[stats]") {
    Loadout Gear;
    Gear.Items.push_back({.Id = "helmet", .Slot = EquipmentSlot::Head,
                          .Covers = {BodyPart::Head}, .MassKg = 2.0f, .Armor = 0.3f});
    const auto Balance = BalanceTable::getDefaults();
    const auto Base = computeProfile({}, {}, Balance);
    const auto Armored = computeProfile({}, Gear, Balance);

    const auto Head = static_cast<std::size_t>(BodyPart::Head);
    const auto Torso = static_cast<std::size_t>(BodyPart::Torso);
    CHECK(Armored.Parts[Head].MassKg == Base.Parts[Head].MassKg + 2.0f);
    CHECK(Armored.Parts[Head].Armor == 0.3f);
    CHECK(Armored.Parts[Torso].MassKg == Base.Parts[Torso].MassKg);
}
