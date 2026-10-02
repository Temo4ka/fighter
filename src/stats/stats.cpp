#include "stats/stats.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

namespace fighter::stats {

BalanceTable BalanceTable::getDefaults() {
    BalanceTable Table;
    auto Set = [&](BodyPart Part, float Kg) { Table.BaseMassKg[static_cast<size_t>(Part)] = Kg; };
    Set(BodyPart::Head, 5.0f);
    Set(BodyPart::Torso, 26.0f);
    Set(BodyPart::Pelvis, 11.0f);
    Set(BodyPart::UpperArmL, 2.0f);
    Set(BodyPart::ForearmL, 1.6f);
    Set(BodyPart::UpperArmR, 2.0f);
    Set(BodyPart::ForearmR, 1.6f);
    Set(BodyPart::ThighL, 7.5f);
    Set(BodyPart::ShinL, 3.5f);
    Set(BodyPart::FootL, 1.1f);
    Set(BodyPart::ThighR, 7.5f);
    Set(BodyPart::ShinR, 3.5f);
    Set(BodyPart::FootR, 1.1f);
    return Table;
}

PhysicalProfile computeProfile(const Stats& BaseStats, const Loadout& Gear, const BalanceTable& Balance) {
    PhysicalProfile Profile;

    const float ConScale = 1.0f + Balance.MassPerCon * static_cast<float>(BaseStats.Constitution - 10);
    for (auto&& [Params, BaseMass] : std::views::zip(Profile.Parts, Balance.BaseMassKg)) {
        Params.MassKg = BaseMass * std::max(ConScale, 0.5f);
    }

    for (const EquipmentItem& Item : Gear.Items) {
        if (Item.Covers.empty()) continue;
        const float Share = Item.MassKg / static_cast<float>(Item.Covers.size());
        for (BodyPart Part : Item.Covers) {
            PartParams& Params = Profile.Parts[static_cast<size_t>(Part)];
            Params.MassKg += Share;
            Params.Armor = std::clamp(Params.Armor + Item.Armor, 0.0f, 0.9f);
        }
    }

    Profile.MotorMaxTorque =
        Balance.BaseMotorTorque * (1.0f + Balance.TorquePerStr * static_cast<float>(BaseStats.Strength - 10));
    Profile.MotorGain =
        Balance.BaseMotorGain * (1.0f + Balance.GainPerDex * static_cast<float>(BaseStats.Dexterity - 10));
    Profile.MoveSpeedScale =
        std::max(1.0f + Balance.MoveSpeedPerDex * static_cast<float>(BaseStats.Dexterity - 10), 0.5f);
    Profile.MaxHp = Balance.BaseHp + Balance.HpPerCon * static_cast<float>(BaseStats.Constitution - 10);
    return Profile;
}

} // namespace fighter::stats
