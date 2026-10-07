#include "stats/equipment.hpp"

#include <cstddef>
#include <format>
#include <utility>
#include <vector>

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

Loadout buildLoadout(std::span<const ItemRef> Items, const ItemCatalog& Catalog) {
    Loadout Gear;
    Gear.Items.reserve(Items.size());
    for (const ItemRef& Ref : Items) {
        const EquipmentItem* Found = Catalog.findItem(Ref.Id);
        if (Found == nullptr) throw DataError(std::format("unknown item id '{}'", Ref.Id));
        EquipmentItem Item = *Found;
        if (Ref.Slot && *Ref.Slot != Item.Slot) {
            // Only a one-handed item changes hands.
            if (!isHandSlot(*Ref.Slot) || !isHandSlot(Item.Slot) || Item.TwoHanded) {
                throw DataError(std::format("item '{}' cannot be put into the {} slot", Item.Id,
                                            getEquipmentSlotName(*Ref.Slot)));
            }
            Item.Slot = *Ref.Slot;
        }
        if (isHandSlot(Item.Slot)) {
            Item.Covers = {getHandPart(Item.Slot)};
            if (Item.TwoHanded) Item.Covers.push_back(getHandPart(EquipmentSlot::OffHand));
        }
        Gear.Items.push_back(std::move(Item));
    }
    validateLoadout(Gear);
    return Gear;
}

Loadout buildLoadout(std::span<const std::string> ItemIds, const ItemCatalog& Catalog) {
    std::vector<ItemRef> Items;
    for (const std::string& Id : ItemIds) Items.push_back({.Id = Id, .Slot = std::nullopt});
    return buildLoadout(Items, Catalog);
}

std::string_view getEquipmentSlotName(EquipmentSlot Slot) {
    switch (Slot) {
        case EquipmentSlot::Head: return "Head";
        case EquipmentSlot::Body: return "Body";
        case EquipmentSlot::Hands: return "Hands";
        case EquipmentSlot::Legs: return "Legs";
        case EquipmentSlot::Feet: return "Feet";
        case EquipmentSlot::MainHand: return "MainHand";
        case EquipmentSlot::OffHand: return "OffHand";
    }
    return "?";
}

std::optional<EquipmentSlot> findEquipmentSlot(std::string_view Name) {
    for (EquipmentSlot Slot : EquipmentSlots) {
        if (getEquipmentSlotName(Slot) == Name) return Slot;
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
