#pragma once

#include <string>
#include <vector>

#include "core/body.hpp"

// Статы и снаряжение бойца, перевод их в физические параметры
// (docs/DEVELOPMENT_PLAN.md §4, агент E). Чистая логика: без SFML и Box2D.
namespace fighter::stats {

// Базовые RPG-статы. Новые появятся вместе с магией и способностями.
struct Stats {
    int strength = 10;      // STR: момент моторов, сила удара
    int dexterity = 10;     // DEX: скорость выхода на позу, скорость клипов
    int constitution = 10;  // CON: масса, HP, сопротивление отбрасыванию
};

enum class EquipmentSlot { Head, Body, Hands, Legs, Feet, Weapon };

struct EquipmentItem {
    std::string id;
    EquipmentSlot slot = EquipmentSlot::Body;
    std::vector<BodyPart> covers;   // какие части тела утяжеляет и защищает
    float massKg = 0.0f;            // масса добавляется к каждой части из covers поровну
    float armor = 0.0f;             // 0..1: доля поглощаемого урона
};

struct Loadout {
    std::vector<EquipmentItem> items;
};

// Физические параметры одной части тела после учёта статов и снаряжения.
struct PartParams {
    float massKg = 0.0f;
    float armor = 0.0f;
};

// Итог «статы + снаряжение → физика». Его применяет rig, его использует combat.
struct PhysicalProfile {
    PerBodyPart<PartParams> parts{};
    float motorMaxTorque = 0.0f;   // Н·м, от STR
    float motorGain = 0.0f;        // 1/с, от DEX: как быстро мотор догоняет позу
    float maxHp = 0.0f;            // от CON
};

// Коэффициенты баланса. В фазе 2 загружается из data/balance.json.
struct BalanceTable {
    PerBodyPart<float> baseMassKg{};   // масса частей тела при CON = 10
    float massPerCon = 0.03f;          // +3% массы за единицу CON сверх 10
    float baseMotorTorque = 150.0f;
    float torquePerStr = 0.06f;
    float baseMotorGain = 12.0f;
    float gainPerDex = 0.05f;
    float baseHp = 100.0f;
    float hpPerCon = 8.0f;

    // Таблица по умолчанию: тело ~75 кг при CON = 10.
    static BalanceTable defaults();
};

// ЗАГЛУШКА фазы 0: линейные формулы. Агент E заменяет на формулы из таблицы баланса.
PhysicalProfile computeProfile(const Stats& stats, const Loadout& loadout, const BalanceTable& balance);

} // namespace fighter::stats
