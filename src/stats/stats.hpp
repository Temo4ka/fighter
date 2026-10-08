//===- stats/stats.hpp - Fighter stats and equipment ------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the stats and equipment of a fighter and the function
/// that turns them into physical parameters (docs/DEVELOPMENT_PLAN.md,
/// section 4, agent E).
///
/// Pure logic: no SFML and no Box2D.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/body.hpp"

namespace fighter::stats {

/// Physical parameters of one body part after stats and equipment.
struct PartParams {
    float MassKg = 0.0f;
    float Armor = 0.0f;
};

/// The result of "stats + equipment -> physics". The rig applies it and
/// combat uses it.
struct PhysicalProfile {
    PerBodyPart<PartParams> Parts{};
    float MotorMaxTorque = 0.0f;   ///< N*m, from STR.
    float MotorGain = 0.0f;        ///< 1/s, from DEX: how fast a motor reaches the pose.
    float MoveSpeedScale = 1.0f;   ///< From DEX and the equipment mass: multiplies the walking speed of the rig.
    /// From DEX and the weapon: multiplies the playback speed of strikes;
    /// combat keeps the result within each move's limits (O.7).
    float AttackSpeedScale = 1.0f;
    float MaxHp = 0.0f;            ///< From CON.
    /// From CON and armor: multiplies the strength thresholds of the reaction
    /// levels (data/reactions.json), so a sturdy fighter reacts less.
    float Poise = 1.0f;
    float MaxStamina = 0.0f;       ///< From CON (O.13).
    float StaminaRegen = 0.0f;     ///< Stamina per second, from CON.
};

/// Base RPG stats. New ones will come together with magic and abilities.
struct Stats {
    int Strength = 10;      ///< STR: motor torque, strike power.
    int Dexterity = 10;     ///< DEX: how fast a pose is reached, clip speed.
    int Constitution = 10;  ///< CON: mass, HP, knockback resistance.
};

/// Where an item is worn. MainHand is the lead (front, L) hand, OffHand the
/// rear (R) one; a two-handed item takes both.
enum class EquipmentSlot { Head, Body, Hands, Legs, Feet, MainHand, OffHand };

/// Is the slot a hand (MainHand, OffHand)?
constexpr bool isHandSlot(EquipmentSlot Slot) {
    return Slot == EquipmentSlot::MainHand || Slot == EquipmentSlot::OffHand;
}

/// The weapon component of an item (decision O.12): how its strikes differ.
struct WeaponProps {
    float ReachM = 0.0f;       ///< How far it sticks out beyond the fist, m.
    float SpeedScale = 1.0f;   ///< Multiplies the speed of the strikes made with it.
    float PowerScale = 1.0f;   ///< Multiplies the damage of the strikes made with it.
    /// The width of the blade or haft, m (the picture's and, halved, the
    /// radius of its capsule), and its default angle to the forearm (the
    /// wrist when no clip sets it), degrees; nullopt: the rig's "weapon"
    /// mount (data/rigs/).
    std::optional<float> WidthM;
    std::optional<float> AngleDeg;
};

/// The shield component of an item: a plate on the forearm that holds it, a
/// part of that forearm for physics and hits. What it does depends on the
/// hand (decision 2026-10-08): in the off hand it is a guard (a hit on the
/// plate counts as blocked) and gives no strikes; in the main hand it is a
/// weapon (its moveset's strikes, no automatic block) and adds PoiseBonus.
/// The kind of a shield (small, medium, large) is only the size of its plate.
struct ShieldProps {
    float LengthM = 0.0f;   ///< Along the forearm, m.
    float WidthM = 0.0f;    ///< Across it (the plate's thickness is fixed), m.
    float AngleDeg = 0.0f;  ///< To the forearm, degrees.
    /// Held in the main hand: poise x (1 + PoiseBonus) (computeProfile()).
    float PoiseBonus = 0.0f;
};

/// An item is the common fields plus optional components (decision
/// 2026-10-08: new items are data only). docs/DATA_FORMATS.md, items/.
struct EquipmentItem {
    std::string Id;
    std::string Name;               ///< Display name for menus and the debug panel.
    EquipmentSlot Slot = EquipmentSlot::Body;
    /// Takes both hands; only with Slot MainHand.
    bool TwoHanded = false;
    /// Body parts that get heavier and protected. Empty in the catalog for
    /// an item held in a hand: buildLoadout() puts it on the holding forearm.
    std::vector<BodyPart> Covers;
    float MassKg = 0.0f;            ///< Split evenly between the parts in Covers.
    float Armor = 0.0f;             ///< 0..1: fraction of damage absorbed.
    /// The moveset the item gives its holder (data/movesets/); only for an
    /// item held in a hand; empty: none.
    std::string MoveSet;
    std::optional<WeaponProps> Weapon;   ///< Only for an item held in a hand.
    std::optional<ShieldProps> Shield;   ///< Only for an item held in a hand.

    /// Does the item take \p Which (a two-handed item takes both hands)?
    bool takesSlot(EquipmentSlot Which) const {
        return Which == Slot || (TwoHanded && Which == EquipmentSlot::OffHand);
    }
};

/// The forearm a hand slot holds items with: MainHand the lead (L), OffHand
/// the rear (R).
constexpr BodyPart getHandPart(EquipmentSlot Hand) {
    return Hand == EquipmentSlot::OffHand ? BodyPart::ForearmR : BodyPart::ForearmL;
}

struct Loadout {
    std::vector<EquipmentItem> Items;

    /// The item that takes \p Which, or nullptr.
    const EquipmentItem* findInSlot(EquipmentSlot Which) const;
    /// The weapon in the main hand (the one strikes use until M.2), or
    /// nullptr.
    const WeaponProps* findWeapon() const;
    /// The moveset of the item in \p Hand; empty if none. A two-handed item
    /// gives its set to the main hand only.
    std::string_view getMoveSet(EquipmentSlot Hand) const;
    /// The shield held in \p Hand (the item's own slot, not the second hand
    /// of a two-handed item), or nullptr.
    const ShieldProps* findShield(EquipmentSlot Hand) const;
};

/// Balance coefficients, loaded from data/balance.json (loadBalanceTable()).
/// computeProfile() uses nothing else, so changing the balance is a JSON edit.
/// Every "per" coefficient multiplies the distance of a stat from 10, the
/// stat of the base body.
struct BalanceTable {
    PerBodyPart<float> BaseMassKg{};   ///< Body part masses at CON = 10.
    float MassPerCon = 0.03f;          ///< +3% mass per CON point above 10.
    float BaseMotorTorque = 150.0f;
    float TorquePerStr = 0.06f;
    float BaseMotorGain = 12.0f;
    float GainPerDex = 0.05f;
    float MoveSpeedPerDex = 0.03f;     ///< +3% walking speed per DEX point above 10.
    /// -0.5% walking speed per kg of equipment: heavy armor slows the walk.
    float MoveSpeedPerGearKg = 0.005f;
    float MoveSpeedMin = 0.5f;         ///< Walking speed scale stays within [Min, Max].
    float MoveSpeedMax = 1.5f;
    /// +2.5% strike speed per DEX point above 10: +-25% at DEX 0 and 20 (O.7).
    float AttackSpeedPerDex = 0.025f;
    float AttackSpeedMin = 0.75f;      ///< The O.7 corridor: stats beyond DEX 0..20 do not widen it.
    float AttackSpeedMax = 1.25f;
    float BaseHp = 100.0f;
    float HpPerCon = 8.0f;
    float BasePoise = 1.0f;
    float PoisePerCon = 0.03f;
    /// +150% poise at average armor 1 (the mass-weighted mean over the body).
    float PoisePerArmor = 1.5f;
    float MaxPartArmor = 0.9f;         ///< Armor of one body part never exceeds this, however many items stack.
    float BaseStamina = 100.0f;
    float StaminaPerCon = 5.0f;
    float BaseStaminaRegen = 20.0f;    ///< Per second at CON = 10.
    float StaminaRegenPerCon = 0.03f;  ///< +3% regeneration per CON point above 10.

    /// The same values as data/balance.json (a test keeps them equal): a body
    /// of about 75 kg at CON = 10. For tests and tools that need no files.
    static BalanceTable getDefaults();
};

/// Turns stats and equipment into the physical parameters of one fighter.
///
///  - Part masses: base mass x CON scale, plus the equipment mass split over
///    the covered parts. Part armor: the sum of the covering items, capped.
///  - Motor torque from STR, motor gain from DEX.
///  - Walking speed from DEX, slowed by the equipment mass; strike speed
///    from DEX; each within its corridor. The weapon's own speed scale is applied by combat per move.
///  - Max HP, max stamina and stamina regeneration from CON.
///  - Poise from CON and the mean armor of the body: armored fighters react
///    less (it multiplies the reaction thresholds); a shield in the main
///    hand multiplies it by 1 + its PoiseBonus.
PhysicalProfile computeProfile(const Stats& BaseStats, const Loadout& Gear, const BalanceTable& Balance);

/// The sum of the masses of all body parts, kg.
float getTotalMassKg(const PhysicalProfile& Profile);

/// The mean armor of the body, weighted by the base part masses (the torso
/// counts more than a foot).
float getMeanArmor(const PhysicalProfile& Profile, const BalanceTable& Balance);

} // namespace fighter::stats
