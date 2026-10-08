//===- stats/validation.hpp - Allowed ranges of stats and items -*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares DataError, the allowed ranges of stats and equipment,
/// and the functions that check a value against them.
///
/// The JSON loaders (stats/loading.hpp) call these checks, but they work on
/// plain structs, so a configuration built in code is checked the same way.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "stats/stats.hpp"

namespace fighter::stats {

/// Invalid stats, item, loadout or data file. The message names the offending
/// field and value and, when the data comes from disk, the file.
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Calls Body and returns its result. If Body throws DataError, throws a new
/// DataError with "Context: " prepended to the message, for example the file
/// name or "items[2]".
template <class Callable>
decltype(auto) withErrorContext(std::string_view Context, Callable&& Body) {
    try {
        return std::forward<Callable>(Body)();
    } catch (const DataError& Error) {
        throw DataError(std::string(Context) + ": " + Error.what());
    }
}

/// Every base stat (STR, DEX, CON) lies in [MinStatValue, MaxStatValue].
inline constexpr int MinStatValue = 1;
inline constexpr int MaxStatValue = 30;

/// The mass of one item lies in [0, MaxItemMassKg].
inline constexpr float MaxItemMassKg = 30.0f;

/// The armor of one item lies in [0, MaxItemArmor].
inline constexpr float MaxItemArmor = 1.0f;

/// A weapon reaches at most MaxWeaponReachM beyond the fist.
inline constexpr float MaxWeaponReachM = 1.5f;

/// The largest weapon radius and shield length or width, m.
inline constexpr float MaxWeaponRadiusM = 0.1f;
inline constexpr float MaxShieldSizeM = 1.2f;
/// A shield in the main hand at most triples poise.
inline constexpr float MaxShieldPoiseBonus = 2.0f;

/// The speed and power scales of a weapon lie in [MinWeaponScale, MaxWeaponScale].
inline constexpr float MinWeaponScale = 0.25f;
inline constexpr float MaxWeaponScale = 4.0f;

/// The total mass of a loadout must not exceed MaxLoadoutMassKg. Large mass
/// ratios make the ragdoll jitter (docs/DEVELOPMENT_PLAN.md, section 9).
inline constexpr float MaxLoadoutMassKg = 60.0f;

/// Throws DataError if a stat is out of [MinStatValue, MaxStatValue].
void validateStats(const Stats& BaseStats);

/// Throws DataError if the item has an empty id, covers no body parts (an
/// item held in a hand may: buildLoadout() covers its forearm), covers an
/// unknown body part or one part twice, or has mass or armor out of range;
/// if it is two-handed outside MainHand; if it has a moveset, a weapon or a
/// shield but is not held in a hand; or if a weapon or shield size, angle or
/// scale is out of range.
void validateItem(const EquipmentItem& Item);

/// One scalar coefficient of BalanceTable under its key in data/balance.json.
/// (base_mass_kg is a table of its own and not listed here.)
struct BalanceField {
    std::string_view Key;
    float BalanceTable::* Member;
    bool MustBePositive;   ///< Otherwise the value may also be zero.
};

/// Every scalar coefficient of BalanceTable. The loader reads exactly these
/// keys (and base_mass_kg); validateBalanceTable() checks them.
std::span<const BalanceField> getBalanceFields();

/// Throws DataError, naming the key and the value, if a coefficient is not
/// finite, a part mass or a base value is not positive, a "per" coefficient
/// is negative, a corridor has min above max or max_part_armor is out of (0, 1].
void validateBalanceTable(const BalanceTable& Balance);

/// Validates every item and throws DataError if two items take the same slot
/// or their total mass exceeds MaxLoadoutMassKg.
void validateLoadout(const Loadout& Gear);

} // namespace fighter::stats
