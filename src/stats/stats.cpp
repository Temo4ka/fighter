#include "stats/stats.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

namespace fighter::stats {
namespace {

/// The stat value of the base body: every "per stat" coefficient counts from it.
constexpr int BaseStatValue = 10;
/// Safety floors: a stat inside its validated range never gets here with the
/// shipped balance, but an edited balance file must not give a negative body.
constexpr float MinMassScale = 0.5f;
constexpr float MinResource = 1.0f;   ///< HP, stamina.
constexpr float MinPoise = 0.1f;

} // namespace

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
    // Distance of each stat from the stat of the base body.
    const auto Str = static_cast<float>(BaseStats.Strength - BaseStatValue);
    const auto Dex = static_cast<float>(BaseStats.Dexterity - BaseStatValue);
    const auto Con = static_cast<float>(BaseStats.Constitution - BaseStatValue);

    const float MassScale = std::max(1.0f + Balance.MassPerCon * Con, MinMassScale);
    for (auto&& [Params, BaseMass] : std::views::zip(Profile.Parts, Balance.BaseMassKg)) {
        Params.MassKg = BaseMass * MassScale;
    }
    for (const EquipmentItem& Item : Gear.Items) {
        if (Item.Covers.empty()) continue;
        const float Share = Item.MassKg / static_cast<float>(Item.Covers.size());
        for (BodyPart Part : Item.Covers) {
            PartParams& Params = Profile.Parts[static_cast<size_t>(Part)];
            Params.MassKg += Share;
            Params.Armor = std::clamp(Params.Armor + Item.Armor, 0.0f, Balance.MaxPartArmor);
        }
    }

    Profile.MotorMaxTorque = Balance.BaseMotorTorque * std::max(1.0f + Balance.TorquePerStr * Str, 0.0f);
    Profile.MotorGain = Balance.BaseMotorGain * std::max(1.0f + Balance.GainPerDex * Dex, 0.0f);
    Profile.MoveSpeedScale =
        std::clamp(1.0f + Balance.MoveSpeedPerDex * Dex, Balance.MoveSpeedMin, Balance.MoveSpeedMax);
    Profile.AttackSpeedScale =
        std::clamp(1.0f + Balance.AttackSpeedPerDex * Dex, Balance.AttackSpeedMin, Balance.AttackSpeedMax);

    Profile.MaxHp = std::max(Balance.BaseHp + Balance.HpPerCon * Con, MinResource);
    Profile.Poise = std::max(Balance.BasePoise * (1.0f + Balance.PoisePerCon * Con) *
                                 (1.0f + Balance.PoisePerArmor * getMeanArmor(Profile, Balance)),
                             MinPoise);
    Profile.MaxStamina = std::max(Balance.BaseStamina + Balance.StaminaPerCon * Con, MinResource);
    Profile.StaminaRegen = Balance.BaseStaminaRegen * std::max(1.0f + Balance.StaminaRegenPerCon * Con, 0.0f);
    return Profile;
}

float getTotalMassKg(const PhysicalProfile& Profile) {
    float Sum = 0.0f;
    for (const PartParams& Params : Profile.Parts) Sum += Params.MassKg;
    return Sum;
}

float getMeanArmor(const PhysicalProfile& Profile, const BalanceTable& Balance) {
    float Weighted = 0.0f;
    float Total = 0.0f;
    for (auto&& [Params, BaseMass] : std::views::zip(Profile.Parts, Balance.BaseMassKg)) {
        Weighted += Params.Armor * BaseMass;
        Total += BaseMass;
    }
    return Total > 0.0f ? Weighted / Total : 0.0f;
}

const WeaponProps* Loadout::findWeapon() const {
    for (const EquipmentItem& Item : Items) {
        if (Item.Weapon) return &*Item.Weapon;
    }
    return nullptr;
}

} // namespace fighter::stats
