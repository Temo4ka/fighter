//===- combat/held_items.hpp - Items held in the hands ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares getHeldItems(), which turns the items in a fighter's
/// hands into what the rig shapes (rig::HeldItem). Battle builds the bodies
/// with it, the pose editor draws the same items on its ghost.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/equipment.hpp"

namespace fighter::combat {

/// The weapons and shields of the items in both hands, on the forearms of
/// \p Mount (main hand: Part, the other: OffPart). A two-handed item is held
/// by the main hand.
std::vector<rig::HeldItem> getHeldItems(const stats::Loadout& Gear, const rig::WeaponMount& Mount);

} // namespace fighter::combat
