#include <catch2/catch_test_macros.hpp>

#include <numeric>

#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::stats;

namespace {
float totalMass(const PhysicalProfile& p) {
    float sum = 0.0f;
    for (const PartParams& part : p.parts) sum += part.massKg;
    return sum;
}
} // namespace

TEST_CASE("computeProfile: base fighter weighs about 75 kg", "[stats]") {
    const auto p = computeProfile({}, {}, BalanceTable::defaults());
    CHECK(totalMass(p) > 70.0f);
    CHECK(totalMass(p) < 80.0f);
    CHECK(p.maxHp > 0.0f);
}

TEST_CASE("computeProfile: stats move parameters in the right direction", "[stats]") {
    const auto balance = BalanceTable::defaults();
    const auto base = computeProfile({}, {}, balance);

    CHECK(computeProfile({.strength = 15}, {}, balance).motorMaxTorque > base.motorMaxTorque);
    CHECK(computeProfile({.dexterity = 15}, {}, balance).motorGain > base.motorGain);
    const auto tough = computeProfile({.constitution = 15}, {}, balance);
    CHECK(totalMass(tough) > totalMass(base));
    CHECK(tough.maxHp > base.maxHp);
}

TEST_CASE("computeProfile: equipment adds mass and armor to its body parts", "[stats]") {
    Loadout loadout;
    loadout.items.push_back({.id = "helmet", .slot = EquipmentSlot::Head,
                             .covers = {BodyPart::Head}, .massKg = 2.0f, .armor = 0.3f});
    const auto balance = BalanceTable::defaults();
    const auto base = computeProfile({}, {}, balance);
    const auto armored = computeProfile({}, loadout, balance);

    const auto head = static_cast<std::size_t>(BodyPart::Head);
    const auto torso = static_cast<std::size_t>(BodyPart::Torso);
    CHECK(armored.parts[head].massKg == base.parts[head].massKg + 2.0f);
    CHECK(armored.parts[head].armor == 0.3f);
    CHECK(armored.parts[torso].massKg == base.parts[torso].massKg);
}
