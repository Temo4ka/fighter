#include "stats/validation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <string_view>

#include "core/body.hpp"
#include "stats/equipment.hpp"

namespace fighter::stats {
namespace {

void checkStat(std::string_view Name, int Value);
void checkItemRange(const EquipmentItem& Item, std::string_view Name, float Value, float Max,
                    std::string_view Unit);
void checkBalanceValue(std::string_view Key, float Value, bool MustBePositive);
void checkCorridor(std::string_view MinKey, float Min, std::string_view MaxKey, float Max);

} // namespace

void validateStats(const Stats& BaseStats) {
    checkStat("strength", BaseStats.Strength);
    checkStat("dexterity", BaseStats.Dexterity);
    checkStat("constitution", BaseStats.Constitution);
}

std::span<const BalanceField> getBalanceFields() {
    static constexpr std::array<BalanceField, 22> Fields = {{
        {"mass_per_con", &BalanceTable::MassPerCon, false},
        {"base_motor_torque", &BalanceTable::BaseMotorTorque, true},
        {"torque_per_str", &BalanceTable::TorquePerStr, false},
        {"base_motor_gain", &BalanceTable::BaseMotorGain, true},
        {"gain_per_dex", &BalanceTable::GainPerDex, false},
        {"move_speed_per_dex", &BalanceTable::MoveSpeedPerDex, false},
        {"move_speed_per_gear_kg", &BalanceTable::MoveSpeedPerGearKg, false},
        {"move_speed_min", &BalanceTable::MoveSpeedMin, true},
        {"move_speed_max", &BalanceTable::MoveSpeedMax, true},
        {"attack_speed_per_dex", &BalanceTable::AttackSpeedPerDex, false},
        {"attack_speed_min", &BalanceTable::AttackSpeedMin, true},
        {"attack_speed_max", &BalanceTable::AttackSpeedMax, true},
        {"base_hp", &BalanceTable::BaseHp, true},
        {"hp_per_con", &BalanceTable::HpPerCon, false},
        {"base_poise", &BalanceTable::BasePoise, true},
        {"poise_per_con", &BalanceTable::PoisePerCon, false},
        {"poise_per_armor", &BalanceTable::PoisePerArmor, false},
        {"max_part_armor", &BalanceTable::MaxPartArmor, true},
        {"base_stamina", &BalanceTable::BaseStamina, true},
        {"stamina_per_con", &BalanceTable::StaminaPerCon, false},
        {"base_stamina_regen", &BalanceTable::BaseStaminaRegen, true},
        {"stamina_regen_per_con", &BalanceTable::StaminaRegenPerCon, false},
    }};
    return Fields;
}

void validateBalanceTable(const BalanceTable& Balance) {
    for (auto&& [Part, Mass] : std::views::zip(std::views::iota(size_t{0}), Balance.BaseMassKg)) {
        checkBalanceValue(std::format("base_mass_kg.{}", getBodyPartName(static_cast<BodyPart>(Part))), Mass, true);
    }
    for (const BalanceField& Field : getBalanceFields()) {
        checkBalanceValue(Field.Key, Balance.*Field.Member, Field.MustBePositive);
    }
    checkCorridor("move_speed_min", Balance.MoveSpeedMin, "move_speed_max", Balance.MoveSpeedMax);
    checkCorridor("attack_speed_min", Balance.AttackSpeedMin, "attack_speed_max", Balance.AttackSpeedMax);
    if (Balance.MaxPartArmor > 1.0f) {
        throw DataError(std::format("field 'max_part_armor': {} is out of (0, 1]", Balance.MaxPartArmor));
    }
}

void validateItem(const EquipmentItem& Item) {
    if (Item.Id.empty()) throw DataError("item id is empty");
    if (Item.Covers.empty()) throw DataError(std::format("item '{}' covers no body parts", Item.Id));

    PerBodyPart<bool> Covered{};
    for (BodyPart Part : Item.Covers) {
        const auto Index = static_cast<size_t>(Part);
        if (Index >= BodyPartCount) {
            throw DataError(std::format("item '{}' covers an unknown body part (index {})", Item.Id, Index));
        }
        if (Covered[Index]) {
            throw DataError(std::format("item '{}' covers {} twice", Item.Id, getBodyPartName(Part)));
        }
        Covered[Index] = true;
    }

    checkItemRange(Item, "mass", Item.MassKg, MaxItemMassKg, " kg");
    checkItemRange(Item, "armor", Item.Armor, MaxItemArmor, "");

    if (!Item.Weapon) return;
    const WeaponProps& Weapon = *Item.Weapon;
    if (Item.Slot != EquipmentSlot::Weapon) {
        throw DataError(std::format("item '{}' has weapon properties but is not in the Weapon slot", Item.Id));
    }
    if (Weapon.MoveSet.empty()) throw DataError(std::format("weapon '{}' has an empty moveset", Item.Id));
    checkItemRange(Item, "reach", Weapon.ReachM, MaxWeaponReachM, " m");
    const auto CheckScale = [&](std::string_view Name, float Scale) {
        if (Scale >= MinWeaponScale && Scale <= MaxWeaponScale) return;
        throw DataError(std::format("weapon '{}': {} {} is out of [{}, {}]", Item.Id, Name, Scale, MinWeaponScale,
                                    MaxWeaponScale));
    };
    CheckScale("speed scale", Weapon.SpeedScale);
    CheckScale("power scale", Weapon.PowerScale);
}

void validateLoadout(const Loadout& Gear) {
    // The item that already takes each slot, indexed by EquipmentSlot.
    std::array<const EquipmentItem*, EquipmentSlots.size()> SlotOwners{};
    float TotalMassKg = 0.0f;

    for (const EquipmentItem& Item : Gear.Items) {
        validateItem(Item);
        TotalMassKg += Item.MassKg;

        const auto SlotIndex = static_cast<size_t>(Item.Slot);
        if (SlotIndex >= SlotOwners.size()) {
            throw DataError(std::format("item '{}' has an unknown slot (index {})", Item.Id, SlotIndex));
        }
        if (const EquipmentItem* Owner = SlotOwners[SlotIndex]) {
            if (Owner->Id == Item.Id) throw DataError(std::format("item '{}' is listed twice", Item.Id));
            throw DataError(std::format("items '{}' and '{}' both take the {} slot", Owner->Id, Item.Id,
                                        getEquipmentSlotName(Item.Slot)));
        }
        SlotOwners[SlotIndex] = &Item;
    }

    if (TotalMassKg > MaxLoadoutMassKg) {
        throw DataError(
            std::format("total item mass {} kg exceeds the limit of {} kg", TotalMassKg, MaxLoadoutMassKg));
    }
}

namespace {

void checkStat(std::string_view Name, int Value) {
    if (Value < MinStatValue || Value > MaxStatValue) {
        throw DataError(std::format("{} {} is out of range [{}, {}]", Name, Value, MinStatValue, MaxStatValue));
    }
}

void checkItemRange(const EquipmentItem& Item, std::string_view Name, float Value, float Max,
                    std::string_view Unit) {
    // Written so that NaN fails the check too.
    if (!(Value >= 0.0f && Value <= Max)) {
        throw DataError(
            std::format("item '{}': {} {}{} is out of range [0, {}]{}", Item.Id, Name, Value, Unit, Max, Unit));
    }
}

void checkBalanceValue(std::string_view Key, float Value, bool MustBePositive) {
    // Written so that NaN fails the check too.
    const bool Valid = std::isfinite(Value) && (MustBePositive ? Value > 0.0f : Value >= 0.0f);
    if (Valid) return;
    throw DataError(std::format("field '{}': {} must be {}", Key, Value, MustBePositive ? "positive" : "not negative"));
}

void checkCorridor(std::string_view MinKey, float Min, std::string_view MaxKey, float Max) {
    if (Min <= Max) return;
    throw DataError(std::format("field '{}': {} is above '{}' {}", MinKey, Min, MaxKey, Max));
}

} // namespace
} // namespace fighter::stats
