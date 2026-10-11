//===- editor/trajectory.hpp - Paths of the striking tip --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the paths of the striking tip that the pose editor
/// draws (docs/DEVELOPMENT_PLAN.md, step Р.4): the tip of the striking part
/// of the clip for the target (the ghost, sampled over the clip) and for the
/// fact (the frames of editor/physics_preview.hpp), each point with the clip
/// time and whether the clip's active phase is on, and findTipSpeed(), the
/// speed of the tip in a measure of the move stand.
///
/// The tip is the one the move stand measures (combat/move_measure.hpp): the
/// end of the part's long axis that is further from the pelvis, and for the
/// forearm of a move made with the weapon the working end of the weapon in
/// the hand. A clip may be played mirrored (the other leg, the other arm), so
/// of the part and its mirror the fact keeps the one whose tip travelled
/// further, as the stand does.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include "anim/clip.hpp"
#include "combat/move_measure.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"

namespace fighter::editor {

/// One point of a path.
struct TipPoint {
    float TimeSec = 0.0f;     ///< Clip time.
    Vec2 Tip;                 ///< World, m; the pelvis at the start of the move is at x = 0 (the fact), or the ghost's own place.
    bool Active = false;      ///< The clip's active phase is on.
};

struct TipPath {
    BodyPart Part = BodyPart::ForearmL;
    std::vector<TipPoint> Points;
    /// The farthest the tip got from where it began, m.
    float TravelM = 0.0f;
};

/// The striking parts of \p Played, in the order of the body parts.
std::vector<BodyPart> findStrikerParts(const anim::Clip& Played);

/// The path of the tip of the target: the clip \p Edited as the fighter plays
/// it (getPlayedClip()), sampled every \p StepSec of clip time and at its end,
/// with the pelvis track in. Of the striking parts the one whose tip travels
/// furthest. \p UseWeapon: the forearm that holds the weapon is tipped with
/// the weapon. Nullopt if the clip has no striking part or \p StepSec <= 0.
std::optional<TipPath> sampleGhostPath(const GhostContext& Context, const anim::Clip& Edited, bool UseWeapon,
                                       float StepSec);

/// The path of the tip of the fact: every frame of the move (it is run to
/// its end), at the frame's clip time. \p Part is the striking part of the
/// ghost's path; of it and its mirror the one that travelled further is kept.
/// Nullopt if the move cannot run or has no frames.
std::optional<TipPath> sampleFactPath(PhysicsPreview& Preview, BodyPart Part, bool UseWeapon);

/// How fast the tip of the farthest path of \p Measure moves, m/s of real
/// time: the greatest speed over a step in the active phase, and the mean
/// speed over the active phase (path length / time). Zero without an active
/// phase or a path.
struct TipSpeed {
    float PeakMps = 0.0f;
    float ActiveMeanMps = 0.0f;
};

TipSpeed findTipSpeed(const combat::MoveMeasure& Measure);

} // namespace fighter::editor
