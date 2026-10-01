#include "stats/stats.hpp"

#include <algorithm>

namespace fighter::stats {

BalanceTable BalanceTable::defaults() {
    BalanceTable t;
    auto set = [&](BodyPart part, float kg) { t.baseMassKg[static_cast<std::size_t>(part)] = kg; };
    set(BodyPart::Head, 5.0f);
    set(BodyPart::Torso, 26.0f);
    set(BodyPart::Pelvis, 11.0f);
    set(BodyPart::UpperArmL, 2.0f);
    set(BodyPart::ForearmL, 1.6f);
    set(BodyPart::UpperArmR, 2.0f);
    set(BodyPart::ForearmR, 1.6f);
    set(BodyPart::ThighL, 7.5f);
    set(BodyPart::ShinL, 3.5f);
    set(BodyPart::FootL, 1.1f);
    set(BodyPart::ThighR, 7.5f);
    set(BodyPart::ShinR, 3.5f);
    set(BodyPart::FootR, 1.1f);
    return t;
}

PhysicalProfile computeProfile(const Stats& stats, const Loadout& loadout, const BalanceTable& balance) {
    PhysicalProfile profile;

    const float conScale = 1.0f + balance.massPerCon * static_cast<float>(stats.constitution - 10);
    for (std::size_t i = 0; i < kBodyPartCount; ++i) {
        profile.parts[i].massKg = balance.baseMassKg[i] * std::max(conScale, 0.5f);
    }

    for (const EquipmentItem& item : loadout.items) {
        if (item.covers.empty()) continue;
        const float share = item.massKg / static_cast<float>(item.covers.size());
        for (BodyPart part : item.covers) {
            PartParams& p = profile.parts[static_cast<std::size_t>(part)];
            p.massKg += share;
            p.armor = std::clamp(p.armor + item.armor, 0.0f, 0.9f);
        }
    }

    profile.motorMaxTorque =
        balance.baseMotorTorque * (1.0f + balance.torquePerStr * static_cast<float>(stats.strength - 10));
    profile.motorGain =
        balance.baseMotorGain * (1.0f + balance.gainPerDex * static_cast<float>(stats.dexterity - 10));
    profile.maxHp = balance.baseHp + balance.hpPerCon * static_cast<float>(stats.constitution - 10);
    return profile;
}

} // namespace fighter::stats
