//===- combat/stand_config.hpp - Settings of the move stand -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares StandConfig, the settings of the move stand and of
/// measureMove() (combat/move_measure.hpp), read from data/stand.json
/// (docs/TUNING.md, "Стенд удара"). They set the test fighter and the
/// dummy, where the dummy stands and how long the run waits for what; none
/// of them is a game rule.
///
/// \code
///   // data/stand.json: every key is optional
///   {"attacker": {"strength": 10, "dexterity": 10, "constitution": 10},
///    "dummy": {"strength": 10, "dexterity": 10, "constitution": 10},
///    "dummy_distance_m": 0.8, "use_move_range": true,
///    "settle_sec": 0.5, "start_timeout_sec": 1.0, "move_timeout_sec": 5.0,
///    "repeat_pause_sec": 0.8, "no_dummy_distance_m": 12.0}
/// \endcode
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include "stats/stats.hpp"

namespace fighter::combat {

struct StandConfig {
    /// The stats of the fighter that performs the move, and of the dummy.
    stats::Stats Attacker{.Strength = 10, .Dexterity = 10, .Constitution = 10};
    stats::Stats Dummy{.Strength = 10, .Dexterity = 10, .Constitution = 10};
    /// The distance between the pelvises at the start, m, when the move
    /// has no working range of its own or UseMoveRange is off.
    float DummyDistanceM = 0.8f;
    /// Put the dummy in the middle of the move's "ai.range_m" if it has one.
    bool UseMoveRange = true;
    /// The fighters stand still this long before the attacker presses the
    /// input, s.
    float SettleSec = 0.5f;
    /// The longest the input is held waiting for the move to start, s.
    float StartTimeoutSec = 1.0f;
    /// The longest a move may take from its start to the end of its
    /// recovery, s.
    float MoveTimeoutSec = 5.0f;
    /// The stand waits this long after a move before the next run, s.
    float RepeatPauseSec = 0.8f;
    /// Without a dummy, the other fighter stands this far away, m.
    float NoDummyDistanceM = 12.0f;
};

/// Parses stand.json text. Every key is optional; an unknown key or a value
/// out of range is an error. Throws std::runtime_error that names the field.
StandConfig parseStandConfig(std::string_view JsonText);

/// Reads and parses a stand file. Throws std::runtime_error that names the file.
StandConfig loadStandConfig(const std::filesystem::path& Path);

} // namespace fighter::combat
