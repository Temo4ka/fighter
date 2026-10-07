//===- anim/playback.hpp - Smooth switching between clips -------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares anim::PoseTransition, which crossfades the pose shown to
/// the rig when the clips behind it change, and describePlayback(), a line of
/// text for the debug panel about what is playing.
///
/// When to call what (the fighter's per-tick control, once per tick):
/// \code
///   // 1. Layer the clips that play now into one full pose (stance below,
///   //    then walk, crouch or block, then the attack or reaction on top).
///   Pose Target = sampleClip(Stance, 0.0f);
///   layerPose(Target, sampleClipAtRate(*Attack, AttackElapsed, Rate));
///
///   // 2. When a clip starts or ends (attack begins, block released,
///   //    reaction over), start a fade from what was shown last tick. The
///   //    fade time is the clip's own (BlendInSec for the clip that starts,
///   //    BlendOutSec for the one that ended) or the blend table's.
///   if (AttackJustStarted) Fade.begin(Shown, Attack->BlendInSec.value_or(TableSec));
///   if (AttackJustEnded) Fade.begin(Shown, Finished->BlendOutSec.value_or(TableSec));
///
///   // 3. Always: advance the fade and give the result to the rig.
///   Shown = Fade.step(Target, Dt);
///   Body.setTargetAngles(Shown.Angles);
/// \endcode
/// Calling begin() while a fade runs is fine: the new fade starts from the
/// pose shown at that moment, so the pose never jumps.
///
/// The fade only changes the pose handed to the motors. Clip time, the active
/// phase and the strikers are not delayed by it.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "anim/clip.hpp"
#include "anim/pose.hpp"

namespace fighter::anim {

/// A crossfade from the pose shown when it began to the live target pose.
/// Plain state, no hidden inputs: the same calls give the same poses.
class PoseTransition {
public:
    /// Starts fading from \p Current to the target of step() over \p FadeSec.
    /// A time that is not positive ends any fade at once (the next step()
    /// returns the target unchanged).
    void begin(const Pose& Current, float FadeSec);

    /// Advances the fade by \p Dt and returns the pose to show: the target
    /// blended with the pose the fade started from. Without a fade it
    /// returns \p Target. A joint missing from the start pose is not blended.
    Pose step(const Pose& Target, float Dt);

    /// The pose step() would show for \p Target at the fade's current
    /// weight, without advancing it: the same fade applied to another target
    /// (the pose of a step without travel, rig::Rig::setTravelPose).
    Pose peek(const Pose& Target) const;

    /// Drops the fade: the next step() returns the target as it is.
    void cancel();

    bool isActive() const { return Active; }

    /// How much of the target is shown: 0 right after begin(), 1 when there
    /// is no fade (or it just ended). The eased weight, the one step() uses.
    float getWeight() const;

private:
    Pose From;
    float DurationSec = 0.0f;
    float ElapsedSec = 0.0f;
    bool Active = false;
};

/// A line for the debug panel: the clip, its time, rate, phase and the fade,
/// for example "jab 0.12/0.44 s x1.25 startup 0.13 s, active, blend 0.40".
/// \p TimeSec is the clip time (not the real time).
std::string describePlayback(const Clip& Source, float TimeSec, float Rate, const PoseTransition& Fade);

} // namespace fighter::anim
