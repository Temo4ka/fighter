#include "stats/stats.hpp"

#include <algorithm>

namespace fighter::stats {

BalanceTable BalanceTable::getDefaults() {
    BalanceTable Table;
    auto Set = [&](BodyPart Part, float Kg) { Table.BaseMassKg[static_cast<std::size_t>(Part)] = Kg; };
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

PhysicalProfile computeProfile(const Stats& S, const Loadout& L, const BalanceTable& Balance) {
    PhysicalProfile Profile;

    const float ConScale = 1.0f + Balance.MassPerCon * static_cast<float>(S.Constitution - 10);
    for (std::size_t I = 0; I < BodyPartCount; ++I) {
        Profile.Parts[I].MassKg = Balance.BaseMassKg[I] * std::max(ConScale, 0.5f);
    }

    for (const EquipmentItem& Item : L.Items) {
        if (Item.Covers.empty()) continue;
        const float Share = Item.MassKg / static_cast<float>(Item.Covers.size());
        for (BodyPart Part : Item.Covers) {
            PartParams& P = Profile.Parts[static_cast<std::size_t>(Part)];
            P.MassKg += Share;
            P.Armor = std::clamp(P.Armor + Item.Armor, 0.0f, 0.9f);
        }
    }

    Profile.MotorMaxTorque =
        Balance.BaseMotorTorque * (1.0f + Balance.TorquePerStr * static_cast<float>(S.Strength - 10));
    Profile.MotorGain =
        Balance.BaseMotorGain * (1.0f + Balance.GainPerDex * static_cast<float>(S.Dexterity - 10));
    Profile.MaxHp = Balance.BaseHp + Balance.HpPerCon * static_cast<float>(S.Constitution - 10);
    return Profile;
}

} // namespace fighter::stats
