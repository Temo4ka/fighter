//===- editor/editor_config.hpp - Thresholds of the editor ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares EditorConfig, the thresholds of the warnings and the
/// sampling step of the trajectories of the pose editor, read from
/// data/editor.json (docs/TUNING.md, section 15). They are checking aids, not
/// game rules: nothing in a battle reads them.
///
/// \code
///   // data/editor.json: every key is optional
///   {"tip_floor_margin_m": 0.02, "limit_margin_deg": 1.0,
///    "divergence_deg": 35.0, "trajectory_step_sec": 0.0167}
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

namespace fighter::editor {

struct EditorConfig {
    /// A tip lower than this below the floor is a warning, m.
    float TipFloorMarginM = 0.02f;
    /// A key angle this close to a joint limit, or past it, is a warning, degrees.
    float LimitMarginDeg = 1.0f;
    /// The fact is this far from the target in a joint during the active
    /// phase: a warning, degrees.
    float DivergenceDeg = 35.0f;
    /// The step at which the ghost's trajectory is sampled, clip seconds.
    float TrajectoryStepSec = 0.0167f;
};

/// Parses editor.json text. Every key is optional; an unknown key or a value
/// out of range is an error. Throws std::runtime_error that names the field.
EditorConfig parseEditorConfig(std::string_view JsonText);

/// Reads and parses a file. Throws std::runtime_error that names the file.
EditorConfig loadEditorConfig(const std::filesystem::path& Path);

/// The config of \p DataDir / "editor.json", or the defaults when the file
/// does not exist. Throws like loadEditorConfig() when it is broken.
EditorConfig loadEditorConfigOrDefault(const std::filesystem::path& DataDir);

} // namespace fighter::editor
