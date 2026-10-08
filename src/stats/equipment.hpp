//===- stats/equipment.hpp - Item catalog and loadouts ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ItemCatalog, the set of known equipment items, and
/// buildLoadout(), which turns a list of item ids into a checked Loadout.
/// It also maps equipment slots to and from their names, as they are written
/// in data files (body part names: findBodyPart() in core/body.hpp).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/body.hpp"
#include "stats/stats.hpp"

namespace fighter::stats {

/// All equipment items a fighter can wear, found by id.
class ItemCatalog {
public:
    /// Validates the item (validateItem()) and adds it. Throws DataError if
    /// the item is invalid or its id is already taken.
    void addItem(EquipmentItem Item);

    /// The item with this id, or nullptr if there is none.
    const EquipmentItem* findItem(std::string_view Id) const;

    /// All items in the order they were added.
    const std::vector<EquipmentItem>& getItems() const { return Items; }

    size_t getSize() const { return Items.size(); }

private:
    std::vector<EquipmentItem> Items;
    std::map<std::string, size_t, std::less<>> IndexById;
};

/// An item of a fighter: its id and, for a one-handed item, the hand it is
/// held in when that is not the item's own slot (a sword in the off hand).
struct ItemRef {
    std::string Id;
    std::optional<EquipmentSlot> Slot;

    bool operator==(const ItemRef&) const = default;
};

/// Looks the items up in the catalog, puts each into its slot (an item held
/// in a hand covers the holding forearm, or both for a two-handed one) and
/// checks the result with validateLoadout(). Throws DataError on an unknown
/// id, a slot an item cannot take, an item listed twice, two items in one
/// slot or too much total mass.
Loadout buildLoadout(std::span<const ItemRef> Items, const ItemCatalog& Catalog);

/// The same for items in their own slots.
Loadout buildLoadout(std::span<const std::string> ItemIds, const ItemCatalog& Catalog);

/// Every equipment slot, in declaration order.
inline constexpr std::array<EquipmentSlot, 7> EquipmentSlots = {
    EquipmentSlot::Head, EquipmentSlot::Body,     EquipmentSlot::Hands,   EquipmentSlot::Legs,
    EquipmentSlot::Feet, EquipmentSlot::MainHand, EquipmentSlot::OffHand,
};

/// The slot name used in data files, for example "Head".
std::string_view getEquipmentSlotName(EquipmentSlot Slot);

/// The slot with this name, or nullopt. Names are case-sensitive.
std::optional<EquipmentSlot> findEquipmentSlot(std::string_view Name);

/// "A, B, C": the names of all slots, for error messages.
std::string listEquipmentSlotNames();

/// "Head, Torso, ...": the names of all body parts, for error messages.
std::string listBodyPartNames();

} // namespace fighter::stats
