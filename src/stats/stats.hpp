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
    float MaxHp = 0.0f;            ///< From CON.
};

/// Base RPG stats. New ones will come together with magic and abilities.
struct Stats {
    int Strength = 10;      ///< STR: motor torque, strike power.
    int Dexterity = 10;     ///< DEX: how fast a pose is reached, clip speed.
    int Constitution = 10;  ///< CON: mass, HP, knockback resistance.
};

enum class EquipmentSlot { Head, Body, Hands, Legs, Feet, Weapon };

struct EquipmentItem {
    std::string Id;
    std::string Name;               ///< Display name for menus and the debug panel.
    EquipmentSlot Slot = EquipmentSlot::Body;
    std::vector<BodyPart> Covers;   ///< Body parts that get heavier and protected.
    float MassKg = 0.0f;            ///< Split evenly between the parts in Covers.
    float Armor = 0.0f;             ///< 0..1: fraction of damage absorbed.
};

struct Loadout {
    std::vector<EquipmentItem> Items;
};

/// Balance coefficients. Loaded from data/balance.json in phase 2.
struct BalanceTable {
    PerBodyPart<float> BaseMassKg{};   ///< Body part masses at CON = 10.
    float MassPerCon = 0.03f;          ///< +3% mass per CON point above 10.
    float BaseMotorTorque = 150.0f;
    float TorquePerStr = 0.06f;
    float BaseMotorGain = 12.0f;
    float GainPerDex = 0.05f;
    float BaseHp = 100.0f;
    float HpPerCon = 8.0f;

    /// The default table: a body of about 75 kg at CON = 10.
    static BalanceTable getDefaults();
};

/// PLACEHOLDER for phase 0: linear formulas. Agent E replaces them with the
/// formulas from the balance table.
PhysicalProfile computeProfile(const Stats& BaseStats, const Loadout& Gear, const BalanceTable& Balance);

} // namespace fighter::stats
