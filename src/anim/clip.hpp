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
///     "blendIn": 0.04,            // optional: fade-in time, s (see PoseTransition)
///     "blendOut": 0.10,           // optional: fade-out time, s
///     "keys": [
///       { "t": 0.00, "pose": { "UpperArmL": 50, "ForearmL": 120 } },
///       { "t": 0.10, "pose": { "UpperArmL": 88, "ForearmL": 4 } }
///     ]
///   }
/// \endcode
/// Every key of a clip sets the same joints; they form the clip's mask.
///
/// A one-shot clip (attack, reaction) plays once and is finished at its
/// duration. A clip with "loop": true is a cycle (walk) or, with a single key,
/// a pose held for as long as the fighter stays in it (crouch, blocks).
///
/// Playback rate: a clip is sampled in its own time; the fighter's speed (DEX)
/// only changes how fast that time runs, see the "rate" helpers below.
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

inline constexpr float DefaultBlendInSec = 0.08f;
inline constexpr float DefaultBlendOutSec = 0.10f;

/// The slowest playback rate the rate helpers accept: a stalled or negative
/// rate would make durations infinite.
inline constexpr float MinPlaybackRate = 0.05f;

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
    /// Fade times, s: how long the pose takes to cross over from the previous
    /// one when this clip starts (BlendInSec) and back to what is below it
    /// when it ends (BlendOutSec). Combat passes them to PoseTransition::begin().
    float BlendInSec = DefaultBlendInSec;
    float BlendOutSec = DefaultBlendOutSec;

    bool isActiveAt(float TimeSec) const { return TimeSec >= ActiveBeginSec && TimeSec < ActiveEndSec; }
    bool isFinishedAt(float TimeSec) const { return !Loop && TimeSec >= DurationSec; }
    bool isStriker(BodyPart Part) const { return Strikers.test(static_cast<size_t>(Part)); }
};

/// The pose at \p TimeSec: keys are interpolated linearly. A looping clip
/// wraps around (the last key blends into the first one); a one-shot clip
/// holds its last key.
Pose sampleClip(const Clip& Source, float TimeSec);

/// Playback rate. A rate of 2 plays the clip twice as fast: it takes half the
/// time and its active phase starts twice as early. Rates below
/// MinPlaybackRate are raised to it. Combat decides the rate (DEX, tired,
/// slowed); these helpers are the mechanics only.

/// The clip time after \p Dt seconds of real time played at \p Rate. A looping
/// clip wraps around; a one-shot clip stops at its duration, where
/// isFinishedAt() becomes true.
float advanceClipTime(const Clip& Source, float TimeSec, float Dt, float Rate);

/// The pose \p ElapsedSec of real time after the clip started, played at \p Rate.
Pose sampleClipAtRate(const Clip& Source, float ElapsedSec, float Rate);

/// How long the whole clip takes at \p Rate, real seconds.
float getDurationAtRate(const Clip& Source, float Rate);

/// Real seconds from the start of the clip to its active phase at \p Rate:
/// the startup the opponent sees (decision O.7).
float getStartupAtRate(const Clip& Source, float Rate);

/// Real seconds from the start of the clip to the end of its active phase.
float getActiveEndAtRate(const Clip& Source, float Rate);

/// The rate at which the active phase starts \p StartupSec after the clip
/// started. 1 when the clip has no active phase or \p StartupSec is not positive.
float getRateForStartup(const Clip& Source, float StartupSec);

/// \p Rate lowered, if needed, so that the active phase starts no sooner than
/// \p MinStartupSec after the clip started. A fighter however fast cannot
/// strike before the floor of the move (MoveDef::MinStartupSec). Unchanged
/// when the clip has no active phase or the floor is not positive.
float limitRateByStartup(const Clip& Source, float Rate, float MinStartupSec);

/// Parses a clip from JSON text. Throws std::runtime_error with a message
/// that names the problem.
Clip parseClip(std::string_view JsonText, std::string Name);

/// Reads and parses a clip file; the clip is named after the file stem.
/// Throws std::runtime_error.
Clip loadClip(const std::filesystem::path& Path);

} // namespace fighter::anim
