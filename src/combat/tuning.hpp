//===- combat/tuning.hpp - Battle tuning loaded from JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CombatTuning, the battle parameters that belong to
/// neither fighter's body: where the fighters start, how close they may get
/// and which contacts count as hits. They are read from data/combat.json when a Battle is created, so
/// a restart (F5 in the sandbox) picks up edited values.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

namespace fighter::combat {

struct CombatTuning {
    /// Distance between the fighters' body origins at the start, m.
    float SpawnDistance = 2.4f;
    /// Closing speed above which a contact between body parts of different
    /// fighters is reported by physics, m/s. Combat then keeps only the
    /// contacts of a striking limb in the active phase of an attack.
    float HitSpeedThreshold = 0.6f;
    /// Half the width of a fighter's pushbox, m: the pelvises stay at least
    /// twice this apart and this far from the arena walls.
    float BodyHalfWidth = 0.25f;
    /// Overlapping pelvises are pushed apart at most this fast, m/s.
    float SeparationSpeed = 4.0f;
};

/// Parses the tuning from JSON text. Every key is optional; an unknown key
/// is an error. Throws std::runtime_error.
CombatTuning parseCombatTuning(std::string_view JsonText);

/// Reads and parses a tuning file. Throws std::runtime_error.
CombatTuning loadCombatTuning(const std::filesystem::path& Path);

} // namespace fighter::combat
