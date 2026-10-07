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

enum class EquipmentSlot { Head, Body, Hands, Legs, Feet, Weapon };

/// What makes an item in the Weapon slot a weapon (decision O.12).
struct WeaponProps {
    /// The moveset of the weapon, data/movesets/<MoveSet>.json: its strikes
    /// and its block.
    std::string MoveSet;
    float ReachM = 0.0f;       ///< How far it sticks out beyond the fist, m.
    float SpeedScale = 1.0f;   ///< Multiplies the speed of strikes.
    float PowerScale = 1.0f;   ///< Multiplies the damage of strikes.
};

struct EquipmentItem {
    std::string Id;
    std::string Name;               ///< Display name for menus and the debug panel.
    EquipmentSlot Slot = EquipmentSlot::Body;
    std::vector<BodyPart> Covers;   ///< Body parts that get heavier and protected.
    float MassKg = 0.0f;            ///< Split evenly between the parts in Covers.
    float Armor = 0.0f;             ///< 0..1: fraction of damage absorbed.
    std::optional<WeaponProps> Weapon;   ///< Only in the Weapon slot; nullopt there is not a weapon yet.
};

struct Loadout {
    std::vector<EquipmentItem> Items;

    /// The properties of the weapon in the loadout, or nullptr if it has none.
    const WeaponProps* findWeapon() const;
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
///    less (it multiplies the reaction thresholds).
PhysicalProfile computeProfile(const Stats& BaseStats, const Loadout& Gear, const BalanceTable& Balance);

/// The sum of the masses of all body parts, kg.
float getTotalMassKg(const PhysicalProfile& Profile);

/// The mean armor of the body, weighted by the base part masses (the torso
/// counts more than a foot).
float getMeanArmor(const PhysicalProfile& Profile, const BalanceTable& Balance);

} // namespace fighter::stats
