//===- app/pose_editor.hpp - Pose editor window -----------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares PoseEditor, the window of the pose editor
/// (fighter_app --pose-editor [<clip>] [--weapon <item>], debug build only).
///
/// The editor edits the clips of data/poses/ with Dear ImGui panels over a
/// scene (docs/DEVELOPMENT_PLAN.md, track "Редактор поз"). Step Р.1: the
/// ghost, the target pose of the clip posed by forward kinematics without
/// physics, with the held item; a time slider; sliders for the angles of the
/// selected key; keys and clip fields; saving. Step Р.2: the mouse turns a
/// joint by its part and moves the fist and the blade tip by handles (arm IK,
/// editor/pose_drag.hpp). Step Р.3: the fact, the same clip played by the
/// physics of the move stand (editor/physics_preview.hpp), drawn over the
/// ghost, with a frame step and the worst joint error. All the logic is in
/// the editor
/// module (src/editor); this class only holds the state and draws the panels.
/// Legs: the joints of the legs are edited like the others (sliders, keys,
/// the mouse turns a leg part), which is how the weapon stances are edited.
/// What is edited is chosen in the window itself (the "Open" panel: a
/// fighter, a clip, an item, the move of the fact), the command line only
/// makes the first choice.
/// Controls are described in docs/TUNING.md.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "core/body.hpp"
#include "anim/pose.hpp"
#include "core/vec2.hpp"
#include "editor/arm_ik.hpp"
#include "editor/catalog.hpp"
#include "editor/ghost.hpp"
#include "editor/physics_preview.hpp"
#include "editor/pose_fk.hpp"
#include "stats/equipment.hpp"

namespace fighter::app {

class PoseEditor {
public:
    struct Options {
        std::filesystem::path Root;   ///< Project root (assets/, data/).
        /// data/poses/<Clip>.json; empty: the general stance ("stance").
        std::string Clip;
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

    /// What is edited and shown with: the clip, the items in the hands and
    /// the move the fact plays (empty: the first that can start).
    struct Selection {
        std::string Clip;
        std::vector<stats::ItemRef> Items;
        std::string Move;
    };

    /// Switches to \p Choice: loads the context and the clip, drops the edits
    /// and restarts the fact. False (and the reason in Status) if something
    /// cannot be read; then nothing has changed.
    bool open(const Selection& Choice);
    /// open(), or, with unsaved edits, asks first (Save / Discard / Cancel).
    void requestOpen(Selection Choice);
    /// Reads the lists of the Open panel and the users of the clip.
    void refreshCatalog();
    /// While the stance clip itself is edited the ghost's base follows it.
    void syncStance();
    void drawOpenPanel();
    void drawUnsavedPopup();
    /// The item that is the weapon in the main hand, for the fact; empty: none.
    std::string findWeaponId() const;

    /// Reads the clip from its file; false (and the reason in Status) if it
    /// cannot be read. Drops the edits.
    bool reload();
    bool save();
    void drawPanels();
    void drawClipPanel();
    void drawTimelinePanel();
    void drawKeyPanel();
    void drawFactPanel();
    /// Hands the clip to the fact layer, unless the mouse or a panel widget
    /// is in the middle of an edit (the fact runs again once it is done).
    void updateFact();
    /// The frame of the fact shown now: the stepped one, or the one at the
    /// clip time; nullptr if none.
    const editor::FactFrame* findFactFrame();
    /// One physics step of the fact forward (+1) or back (-1); the time
    /// follows the frame's clip time.
    void stepFact(int Direction);
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
    std::string ClipName;                          ///< Of the clip edited.
    std::vector<stats::ItemRef> HandItems;         ///< In the hands of the ghost.
    std::string MoveId;                            ///< Chosen for the fact; empty: the first that can.
    std::filesystem::path ClipPath;
    /// The clip has leg keys in its file: keying legs in a clip that had
    /// none changes what the clip is, and the Clip panel says so.
    bool ClipHadLegs = false;
    /// The Open panel's lists and who uses the clip as a stance.
    std::vector<editor::FighterEntry> Fighters;
    std::vector<editor::HandItemEntry> HandItemList;
    std::vector<std::string> ClipList;
    std::vector<editor::MoveCandidate> MoveList;   ///< The moves that play the clip.
    editor::StanceUsers Users;
    std::array<char, 64> ClipFilter{};
    /// The choice waiting for the answer to "unsaved changes".
    std::optional<Selection> Pending;
    bool AskUnsaved = false;
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
    /// The fact layer; empty until the context is loaded.
    std::optional<editor::PhysicsPreview> Fact;
    bool ShowGhost = true;
    bool ShowFact = true;
    /// The frame the frame step chose and the time it set; a time changed
    /// any other way shows the frame of that time again.
    std::optional<size_t> FactStep;
    float FactStepTimeSec = 0.0f;
    /// Where the ghost's pelvis is at the start of the clip, m along the
    /// floor: the fact's pelvis starts there.
    float GhostStartX = 0.0f;
};

} // namespace fighter::app
