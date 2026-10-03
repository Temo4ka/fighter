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

/// The speed and power scales of a weapon lie in [MinWeaponScale, MaxWeaponScale].
inline constexpr float MinWeaponScale = 0.25f;
inline constexpr float MaxWeaponScale = 4.0f;

/// The total mass of a loadout must not exceed MaxLoadoutMassKg. Large mass
/// ratios make the ragdoll jitter (docs/DEVELOPMENT_PLAN.md, section 9).
inline constexpr float MaxLoadoutMassKg = 60.0f;

/// Throws DataError if a stat is out of [MinStatValue, MaxStatValue].
void validateStats(const Stats& BaseStats);

/// Throws DataError if the item has an empty id, covers no body parts, covers
/// an unknown body part or one part twice, or has mass or armor out of range;
/// also if it has weapon properties outside the Weapon slot, an empty weapon
/// class, or a reach or scale out of range.
void validateItem(const EquipmentItem& Item);

/// Validates every item and throws DataError if two items take the same slot
/// or their total mass exceeds MaxLoadoutMassKg.
void validateLoadout(const Loadout& Gear);

} // namespace fighter::stats
