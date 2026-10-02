//===- anim/clip.hpp - Clips of target poses --------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares anim::Clip, a timed sequence of target poses (stance,
/// walk cycle, jab, kick), its sampling and its loading from JSON
/// (data/poses/*.json).
///
/// File format (angles in degrees, times in seconds):
/// \code
///   {
///     "loop": false,
///     "duration": 0.36,
///     "active": [0.08, 0.20],     // optional: the striking phase
///     "strikers": ["ForearmL"],   // optional: parts that hit during it
///     "stiffness": 1.5,           // optional: motor stiffness while playing
///     "allowMove": true,          // optional: may the fighter walk meanwhile
///     "keys": [
///       { "t": 0.00, "pose": { "UpperArmL": 50, "ForearmL": 120 } },
///       { "t": 0.10, "pose": { "UpperArmL": 88, "ForearmL": 4 } }
///     ]
///   }
/// \endcode
/// Every key of a clip sets the same joints; they form the clip's mask.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "anim/pose.hpp"

namespace fighter::anim {

struct Keyframe {
    float TimeSec = 0.0f;
    Pose Target;
};

struct Clip {
    std::string Name;
    std::vector<Keyframe> Keys;   ///< Sorted by time, the first one at 0.
    float DurationSec = 0.0f;     ///< Loop period, or the total length of a one-shot clip.
    bool Loop = false;
    float ActiveBeginSec = 0.0f;  ///< Striking phase; empty when begin == end.
    float ActiveEndSec = 0.0f;
    std::bitset<BodyPartCount> Strikers;  ///< Parts whose contacts are hits in the active phase.
    float Stiffness = 1.0f;       ///< Motor stiffness while the clip plays.
    bool AllowMove = true;

    bool isActiveAt(float TimeSec) const { return TimeSec >= ActiveBeginSec && TimeSec < ActiveEndSec; }
    bool isFinishedAt(float TimeSec) const { return !Loop && TimeSec >= DurationSec; }
    bool isStriker(BodyPart Part) const { return Strikers.test(static_cast<size_t>(Part)); }
};

/// The pose at \p TimeSec: keys are interpolated linearly. A looping clip
/// wraps around (the last key blends into the first one); a one-shot clip
/// holds its last key.
Pose sampleClip(const Clip& Source, float TimeSec);

/// Parses a clip from JSON text. Throws std::runtime_error with a message
/// that names the problem.
Clip parseClip(std::string_view JsonText, std::string Name);

/// Reads and parses a clip file; the clip is named after the file stem.
/// Throws std::runtime_error.
Clip loadClip(const std::filesystem::path& Path);

} // namespace fighter::anim
