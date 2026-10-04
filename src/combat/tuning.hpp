//===- combat/tuning.hpp - Battle tuning loaded from JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares CombatTuning, the battle parameters that belong to
/// neither fighter's body: where the fighters start, how close they may get,
/// which contacts count as hits, stamina, chains and blocking. They are read
/// from data/combat.json when a Battle is created, so a restart (F5 in the
/// sandbox) picks up edited values.
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
    /// With no stamina left, walking and strikes are this much slower (O.13).
    float ExhaustedSpeedScale = 0.7f;
    /// An exhausted fighter is slow until its stamina is back to this share
    /// of the maximum.
    float ExhaustedRecoverFraction = 0.3f;
    /// A strike that hit may be cancelled into the next one of its chain
    /// (MoveDef::ChainTo) this long after its active phase ends, s.
    float ChainWindowSec = 0.3f;
    /// The longest chain, strikes: jab -> jab -> heavy is 3 (O.7).
    int MaxChainLength = 3;
    /// Walking while blocking is this much slower.
    float BlockWalkSpeedScale = 0.5f;
    /// After the end of the fight the bodies keep moving this long without
    /// input, so that a knockout fall plays out, s.
    float EndSettleSec = 1.2f;
};

/// Parses the tuning from JSON text. Every key is optional; an unknown key
/// is an error. Throws std::runtime_error.
CombatTuning parseCombatTuning(std::string_view JsonText);

/// Reads and parses a tuning file. Throws std::runtime_error.
CombatTuning loadCombatTuning(const std::filesystem::path& Path);

} // namespace fighter::combat
