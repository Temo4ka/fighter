//===- editor/clip_check.hpp - Warnings about a clip ------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the warnings of the pose editor (docs/TOOLS.md,
/// section 7.10): what is probably wrong with the clip being edited, found by
/// separate detectors that the window lists and that each give the clip time
/// where the problem is, so that a click can jump to it.
///
///   - the tip of the striking part (the blade, if the weapon is in the
///     hand) below the floor, for the ghost and for the fact;
///   - a joint at its rig limit, or past it, in a key (the ghost would clamp
///     it to the limit);
///   - the clip of a move with no active phase or no striking parts, or a
///     weapon move whose strikers include no forearm (the rules of
///     combat::checkData(), combat::findStrikeProblems());
///   - the fact far from the target in a joint during the active phase.
///
/// The thresholds are in EditorConfig (data/editor.json). A warning is advice
/// for the author, never an error: the editor still saves the clip.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "core/body.hpp"
#include "editor/editor_config.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"
#include "editor/trajectory.hpp"
#include "rig/rig_def.hpp"

namespace fighter::editor {

enum class WarningKind : uint8_t {
    TipBelowFloor,
    JointAtLimit,
    EmptyActive,
    NoStrikers,
    WeaponNotHeld,
    Divergence,
};

/// What the warning is about.
enum class WarningSource : uint8_t {
    Clip,    ///< The clip's keys and fields.
    Ghost,   ///< The target.
    Fact,    ///< The clip played by physics.
};

struct ClipWarning {
    WarningKind Kind = WarningKind::TipBelowFloor;
    WarningSource Source = WarningSource::Clip;
    float TimeSec = 0.0f;                 ///< Clip time where it happens.
    std::optional<BodyPart> Part;         ///< The part or joint, if it is about one.
    std::string Text;                     ///< One line for the list.
};

/// "ghost", "fact" or "clip".
const char* getSourceLabel(WarningSource Source);

/// The tip of \p Path lower than EditorConfig::TipFloorMarginM below the floor
/// (y = 0): one warning, at the lowest point; none if it stays above.
std::vector<ClipWarning> findTipFloorWarnings(const TipPath& Path, WarningSource Source, const EditorConfig& Config);

/// The keys of \p Edited that set a joint (the wrist included) within
/// EditorConfig::LimitMarginDeg of a limit of \p Rig or past it; the root
/// (the lean) has no limit. One warning per key, naming its joints.
std::vector<ClipWarning> findJointLimitWarnings(const rig::RigDef& Rig, const anim::Clip& Edited,
                                                const EditorConfig& Config);

/// The empty active phase, the missing strikers and the weapon move without
/// an arm among them, for a clip played by a move that does (\p UsesWeapon)
/// or does not use the weapon. An active phase that runs past the end of the
/// clip is reported too.
std::vector<ClipWarning> findStrikeWarnings(const anim::Clip& Edited, bool UsesWeapon);

/// The worst divergence of the fact from the target in the frames of
/// \p Preview whose phase is active: if some joint is EditorConfig::DivergenceDeg
/// or more off, one warning at that frame. Runs the move to its end.
std::vector<ClipWarning> findDivergenceWarnings(const GhostContext& Context, const anim::Clip& Edited,
                                                PhysicsPreview& Preview, const EditorConfig& Config);

} // namespace fighter::editor
