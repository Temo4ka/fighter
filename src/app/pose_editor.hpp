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
/// The editor edits the clips of data/poses/ with Dear ImGui panels over the
/// scene of the move stand (docs/DEVELOPMENT_PLAN.md, track "Редактор поз").
/// So far it only opens the window with an empty panel (Р.0).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

namespace fighter::app {

class PoseEditor {
public:
    struct Options {
        std::filesystem::path Root;   ///< Project root (assets/, data/).
        std::string Clip;             ///< data/poses/<Clip>.json.
        std::string Weapon;           ///< data/items/<Weapon>.json; empty: bare hands.
    };

    explicit PoseEditor(Options Settings);
    int run();

private:
    Options Opts;
};

} // namespace fighter::app
