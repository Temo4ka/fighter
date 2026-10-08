#include "stats/describe.hpp"

#include <format>

namespace fighter::stats {
namespace {

std::string describeWeapon(const Loadout& Gear);
/// " (main-hand shield x1.20)" when a shield in the main hand adds poise.
std::string describeShieldPoise(const Loadout& Gear);

} // namespace

std::vector<ProfileLine> describeProfile(const Stats& BaseStats, const Loadout& Gear, const PhysicalProfile& Profile,
                                         const BalanceTable& Balance) {
    float GearKg = 0.0f;
    for (const EquipmentItem& Item : Gear.Items) GearKg += Item.MassKg;

    return {
        {"build", std::format("STR {} DEX {} CON {}, {}", BaseStats.Strength, BaseStats.Dexterity,
                              BaseStats.Constitution, describeWeapon(Gear))},
        {"mass", std::format("{:.1f} kg (gear {:.1f}), armor avg {:.2f}", getTotalMassKg(Profile), GearKg,
                             getMeanArmor(Profile, Balance))},
        {"motors", std::format("torque {:.0f} Nm, gain {:.1f}/s", Profile.MotorMaxTorque, Profile.MotorGain)},
        {"speed", std::format("walk x{:.2f}, strike x{:.2f}", Profile.MoveSpeedScale, Profile.AttackSpeedScale)},
        {"vitals", std::format("HP {:.0f}, poise {:.2f}{}, stamina {:.0f} (+{:.1f}/s)", Profile.MaxHp, Profile.Poise,
                               describeShieldPoise(Gear), Profile.MaxStamina, Profile.StaminaRegen)},
    };
}

namespace {

std::string describeWeapon(const Loadout& Gear) {
    const WeaponProps* Weapon = Gear.findWeapon();
    if (Weapon == nullptr) return "unarmed";
    return std::format("{} (reach +{:.2f} m, speed x{:.2f}, power x{:.2f})", Gear.getMoveSet(EquipmentSlot::MainHand),
                       Weapon->ReachM, Weapon->SpeedScale, Weapon->PowerScale);
}

std::string describeShieldPoise(const Loadout& Gear) {
    const ShieldProps* Shield = Gear.findShield(EquipmentSlot::MainHand);
    if (Shield == nullptr || Shield->PoiseBonus == 0.0f) return {};
    return std::format(" (main-hand shield x{:.2f})", 1.0f + Shield->PoiseBonus);
}

} // namespace
} // namespace fighter::stats
