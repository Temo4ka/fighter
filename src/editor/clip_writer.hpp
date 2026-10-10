//===- editor/clip_writer.hpp - Writing clips back to JSON -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares writeClip(), the small writer of the pose editor that
/// turns an anim::Clip back into the text of a data/poses/*.json file, in the
/// look the files have, so that saving an edited clip gives a small diff
/// (docs/DEVELOPMENT_PLAN.md, track "Редактор поз"). nlohmann::json::dump()
/// cannot do it: it sorts or spreads the keys as it likes.
///
/// The look:
/// \code
///   {
///     "loop": false,
///     "duration": 0.44,
///     "active": [0.16, 0.28],
///     "strikers": ["ForearmL"],
///     "stiffness": 1.6,
///     "allowMove": true,
///     "blendIn": 0.04,
///     "blendOut": 0.08,
///     "pelvisX": [
///       { "t": 0.00, "x": 0.0 },
///       { "t": 0.30, "x": 0.25 }
///     ],
///     "keys": [
///       { "t": 0.00, "pose": { "Torso": -8, "UpperArmL": 45, "ForearmL": 110 } },
///       { "t": 0.09, "pose": { "Torso": -2, "UpperArmL": 30, "ForearmL": 125 } }
///     ]
///   }
/// \endcode
/// Two-space indent, LF, a newline at the end, one key per line. The fields
/// come in a fixed order; "active", "strikers", "blendIn", "blendOut" and
/// "pelvisX" are left out when the clip has none; "stiffness" and "allowMove"
/// are left out when both are at their defaults (1.0, true). The joints of a
/// pose come in a fixed order (the pelvis lean, torso, head, left arm, right
/// arm, the wrist "Weapon", left leg, right leg), angles in degrees without
/// a decimal point when whole. Times of the keys (and of the pelvis track)
/// have as many decimals as the finest of them needs, at least one.
///
/// Numbers are rounded to what a hand-written file has (angles to 0.001
/// degree, times and other values to 0.0001), so that the radians of a loaded
/// clip print as the degrees they were written in.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

#include "anim/clip.hpp"

namespace fighter::editor {

/// The text of the file of \p Source (see the file comment). The clip's name
/// is not written: it is the file's.
std::string writeClip(const anim::Clip& Source);

/// What is wrong with \p Source for a data file, or an empty string: the
/// problem anim::parseClip() finds in the text writeClip() makes of it.
std::string findClipProblem(const anim::Clip& Source);

/// Writes writeClip(\p Source) to \p Path (the whole file at once, via a
/// temporary file next to it). Throws std::runtime_error naming the file, and
/// writes nothing if findClipProblem() finds one.
void saveClip(const anim::Clip& Source, const std::filesystem::path& Path);

} // namespace fighter::editor
