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
/// selected key; keys and clip fields; saving. All the logic is in the editor
/// module (src/editor); this class only holds the state and draws the panels.
/// Controls are described in docs/TUNING.md.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "anim/clip.hpp"
#include "core/body.hpp"
#include "editor/ghost.hpp"

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

    Options Opts;
    editor::GhostContext Context;
    std::filesystem::path ClipPath;
    anim::Clip Edit;
    bool Edited = false;        ///< Unsaved changes.
    std::string Status;         ///< The last message of save/reload, for the panel.
    float TimeSec = 0.0f;       ///< The clip time of the ghost.
    bool Playing = false;
    size_t SelectedKey = 0;
    size_t SelectedPelvisKey = 0;
    std::optional<BodyPart> Highlight;   ///< The part whose slider is under the mouse.
};

} // namespace fighter::app
