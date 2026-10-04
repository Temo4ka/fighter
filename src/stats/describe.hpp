//===- stats/describe.hpp - Stats as text for the debug panel ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares describeProfile(), which turns the stats, the loadout
/// and the resulting PhysicalProfile of a fighter into a few lines of text
/// for the debug panel. The app owns the configs, so it calls this and hands
/// the lines to debug::setPanel(); stats itself does not draw anything.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "stats/stats.hpp"

namespace fighter::stats {

/// One panel line: a short label ("mass") and its text.
struct ProfileLine {
    std::string Label;
    std::string Text;
};

/// The lines "build" (stats and weapon), "mass" (total, gear, mean armor),
/// "motors" (torque, gain), "speed" (walk and strike scales) and "vitals"
/// (HP, poise, stamina, regeneration), in this order. Profile is what
/// computeProfile() returned for BaseStats and Gear with Balance.
std::vector<ProfileLine> describeProfile(const Stats& BaseStats, const Loadout& Gear, const PhysicalProfile& Profile,
                                         const BalanceTable& Balance);

} // namespace fighter::stats
