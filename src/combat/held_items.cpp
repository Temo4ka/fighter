#include "combat/held_items.hpp"

namespace fighter::combat {

std::vector<rig::HeldItem> getHeldItems(const stats::Loadout& Gear, const rig::WeaponMount& Mount) {
    std::vector<rig::HeldItem> Held;
    for (const stats::EquipmentSlot Hand : {stats::EquipmentSlot::MainHand, stats::EquipmentSlot::OffHand}) {
        const stats::EquipmentItem* Item = Gear.findInSlot(Hand);
        // A two-handed item is held by the main hand; the other one grips it.
        if (!Item || Item->Slot != Hand || (!Item->Weapon && !Item->Shield)) continue;
        rig::HeldItem& Entry = Held.emplace_back();
        Entry.Part = Hand == stats::EquipmentSlot::MainHand ? Mount.Part : Mount.OffPart;
        if (Item->Weapon) {
            Entry.WeaponReachM = Item->Weapon->ReachM;
            Entry.WeaponWidthM = Item->Weapon->WidthM;
            Entry.WeaponMassKg = Item->MassKg;
            Entry.WeaponAngleDeg = Item->Weapon->AngleDeg;
            Entry.GripM = Item->Weapon->GripM;
        }
        if (Item->Shield) {
            Entry.ShieldLengthM = Item->Shield->LengthM;
            Entry.ShieldWidthM = Item->Shield->WidthM;
            Entry.ShieldAngleDeg = Item->Shield->AngleDeg;
            // Decision 2026-10-08: only a shield in the off hand guards.
            Entry.ShieldGuards = Hand == stats::EquipmentSlot::OffHand;
        }
    }
    return Held;
}

} // namespace fighter::combat
