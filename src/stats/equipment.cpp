#include "stats/equipment.hpp"

#include <cstddef>
#include <format>
#include <utility>

#include "stats/validation.hpp"

namespace fighter::stats {

// Tables indexed by EquipmentSlot rely on EquipmentSlots being in enum order.
static_assert([] {
    for (size_t Index = 0; Index < EquipmentSlots.size(); ++Index) {
        if (static_cast<size_t>(EquipmentSlots[Index]) != Index) return false;
    }
    return true;
}());

void ItemCatalog::addItem(EquipmentItem Item) {
    validateItem(Item);
    if (findItem(Item.Id) != nullptr) {
        throw DataError(std::format("duplicate item id '{}'", Item.Id));
    }
    IndexById.emplace(Item.Id, Items.size());
    Items.push_back(std::move(Item));
}

const EquipmentItem* ItemCatalog::findItem(std::string_view Id) const {
    const auto Found = IndexById.find(Id);
    return Found != IndexById.end() ? &Items[Found->second] : nullptr;
}

Loadout buildLoadout(std::span<const std::string> ItemIds, const ItemCatalog& Catalog) {
    Loadout Gear;
    Gear.Items.reserve(ItemIds.size());
    for (const std::string& Id : ItemIds) {
        const EquipmentItem* Item = Catalog.findItem(Id);
        if (Item == nullptr) throw DataError(std::format("unknown item id '{}'", Id));
        Gear.Items.push_back(*Item);
    }
    validateLoadout(Gear);
    return Gear;
}

std::string_view getEquipmentSlotName(EquipmentSlot Slot) {
    switch (Slot) {
    case EquipmentSlot::Head: return "Head";
    case EquipmentSlot::Body: return "Body";
    case EquipmentSlot::Hands: return "Hands";
    case EquipmentSlot::Legs: return "Legs";
    case EquipmentSlot::Feet: return "Feet";
    case EquipmentSlot::Weapon: return "Weapon";
    }
    return "?";
}

std::optional<EquipmentSlot> findEquipmentSlot(std::string_view Name) {
    for (EquipmentSlot Slot : EquipmentSlots) {
        if (getEquipmentSlotName(Slot) == Name) return Slot;
    }
    return std::nullopt;
}

std::optional<BodyPart> findBodyPart(std::string_view Name) {
    // Walk the enum instead of keeping a second name table, so a change to
    // BodyPart only needs getBodyPartName() to be updated.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        if (getBodyPartName(Part) == Name) return Part;
    }
    return std::nullopt;
}

std::string listEquipmentSlotNames() {
    std::string Names;
    for (EquipmentSlot Slot : EquipmentSlots) {
        if (!Names.empty()) Names += ", ";
        Names += getEquipmentSlotName(Slot);
    }
    return Names;
}

std::string listBodyPartNames() {
    std::string Names;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Names.empty()) Names += ", ";
        Names += getBodyPartName(static_cast<BodyPart>(Index));
    }
    return Names;
}

} // namespace fighter::stats
