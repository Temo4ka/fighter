//===- app/pose_editor.hpp - Pose editor window -----------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares PoseEditor, the window of the pose editor
/// (fighter_app --pose-editor <clip> [--weapon <item>], debug build only).
///
/// The editor edits the clips of data/poses/ with Dear ImGui panels over a
/// scene (docs/DEVELOPMENT_PLAN.md, track "Редактор поз"). Step Р.1: the
/// ghost, the target pose of the clip posed by forward kinematics without
/// physics, with the held item; a time slider; sliders for the angles of the
/// selected key; keys and clip fields; saving. Step Р.2: the mouse turns a
/// joint by its part and moves the fist and the blade tip by handles (arm IK,
/// editor/pose_drag.hpp). All the logic is in the editor
/// module (src/editor); this class only holds the state and draws the panels.
/// Controls are described in docs/TUNING.md.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "anim/clip.hpp"
#include "core/body.hpp"
#include "anim/pose.hpp"
#include "core/vec2.hpp"
#include "editor/arm_ik.hpp"
#include "editor/ghost.hpp"
#include "editor/pose_fk.hpp"

namespace fighter::app {

class PoseEditor {
public:
    struct Options {
        std::filesystem::path Root;   ///< Project root (assets/, data/).
        std::string Clip;             ///< data/poses/<Clip>.json.
        std::string Weapon;           ///< data/items/<Weapon>.json; empty: bare hands.
        /// Save a frame (the clip at its first key after the first key was
        /// selected) and exit.
        std::optional<std::filesystem::path> Screenshot;
    };

    explicit PoseEditor(Options Settings);
    int run();

private:
    /// What the mouse can take hold of on the ghost.
    enum class GrabKind : uint8_t {
        None,
        Part,   ///< A part: dragging turns its joint.
        Fist,   ///< The fist handle of a forearm: arm IK.
        Tip,    ///< The blade tip handle: the wrist (then the arm).
    };
    struct Grab {
        GrabKind Kind = GrabKind::None;
        BodyPart Part = BodyPart::Torso;   ///< The part, or the forearm of the fist or the weapon.
        bool operator==(const Grab&) const = default;
    };
    struct Drag {
        Grab Target;
        Vec2 From;              ///< Where the drag began, m, on the ghost.
        anim::Pose StartGhost;  ///< The ghost when it began: every frame edits from it.
    };

    /// Reads the clip from its file; false (and the reason in Status) if it
    /// cannot be read. Drops the edits.
    bool reload();
    bool save();
    void drawPanels();
    void drawClipPanel();
    void drawTimelinePanel();
    void drawKeyPanel();
    void markEdited() { Edited = true; }
    void selectKey(size_t Index);
    /// Hover, grab and drag with the mouse at \p Mouse (m, on the ghost:
    /// the pelvis offset taken out); \p PixelsPerMeter of the view.
    void updateMouse(Vec2 Mouse, float PixelsPerMeter);
    void applyDrag(Vec2 Mouse);
    /// What is under \p Mouse on \p Body: a handle first, then a part.
    Grab findGrab(const editor::PosedBody& Body, Vec2 Mouse, float PixelsPerMeter) const;

    Options Opts;
    editor::GhostContext Context;
    std::filesystem::path ClipPath;
    anim::Clip Edit;
    bool Edited = false;        ///< Unsaved changes.
    std::string Status;         ///< The last message of save/reload, for the panel.
    float TimeSec = 0.0f;       ///< The clip time of the ghost.
    bool Playing = false;
    /// How fast the preview plays the clip: real seconds times this are clip
    /// seconds. Only the preview; the clip's times stay as they are.
    float PlaySpeed = 0.25f;
    size_t SelectedKey = 0;
    size_t SelectedPelvisKey = 0;
    std::optional<BodyPart> Highlight;   ///< The part whose slider is under the mouse.
    Grab Hovered;                        ///< Under the mouse, or being dragged.
    std::optional<Drag> Dragging;
    editor::ElbowBend Bend = editor::ElbowBend::Down;   ///< Preferred by the arm IK.
};

} // namespace fighter::app
