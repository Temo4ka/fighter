#include "stats/validation.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string_view>

#include "core/body.hpp"
#include "stats/equipment.hpp"

namespace fighter::stats {
namespace {

void checkStat(std::string_view Name, int Value);
void checkItemRange(const EquipmentItem& Item, std::string_view Name, float Value, float Max,
                    std::string_view Unit);

} // namespace

void validateStats(const Stats& BaseStats) {
    checkStat("strength", BaseStats.Strength);
    checkStat("dexterity", BaseStats.Dexterity);
    checkStat("constitution", BaseStats.Constitution);
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

} // namespace
} // namespace fighter::stats
