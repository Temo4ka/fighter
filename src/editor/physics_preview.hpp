//===- editor/physics_preview.hpp - The clip played by physics --*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares PhysicsPreview, the "fact" layer of the pose editor
/// (docs/DEVELOPMENT_PLAN.md, track "Редактор поз", step Р.3): the clip being
/// edited played by the real physics of the move stand, to be drawn over the
/// ghost (the target), and compareToTarget(), how far the fact is from it.
///
/// The stand runs a move, not a clip (combat/move_measure.hpp), so the
/// preview runs the first move of data/moves whose clip ("clip" or
/// "close_clip") is the edited one and that the held weapon can start
/// (combat::prepareStand()). The move plays the edited clip from memory, not
/// from its file: its text (editor::writeClip(), the text a save would
/// write) is given to the battle instead of the file
/// (combat::BattleConfig::ClipOverrides). A close clip is played with the
/// opponent nearer than the move's "close_range_m".
///
/// Time. One frame of the fact is one physics step of the stand (1/60 s)
/// from the start of the move on; each frame keeps the attack's clip time
/// after its step (combat::FighterView::AttackClipSec). That time starts at
/// 0 the step the move begins and runs at the attack's rate (the fighter's
/// speed and the weapon's), so clip time t of the timeline shows the first
/// frame whose clip time reached t. The fade from the stance into the clip
/// (blendIn) runs in real time from the same step: in its first moments the
/// fact is still on its way from the stance, and that is part of what the
/// layer shows. The settling of the stand before the input is simulated but
/// not kept.
///
/// Cache. A Battle cannot be copied, so the preview keeps the frames, not
/// the physics: the battle is kept at the last frame computed, and a frame
/// further on is simulated on demand. A frame already computed is read from
/// the cache; this is what running the deterministic step from the start
/// again would give. A clip whose text differs from the one the frames were
/// made with, or the dummy switched, drops the cache and the battle: the
/// next request runs from the start (the settling included).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "anim/clip.hpp"
#include "combat/move_measure.hpp"
#include "combat/snapshot.hpp"
#include "core/body.hpp"
#include "editor/pose_fk.hpp"
#include "rig/rig_def.hpp"

namespace fighter::combat {
class Battle;
} // namespace fighter::combat

namespace fighter::editor {

/// What the preview runs.
struct PreviewSettings {
    std::filesystem::path DataDir = "data";
    std::string ClipName;     ///< The edited clip, data/poses/<ClipName>.json.
    /// The item in the main hand (data/items/); empty: bare hands, or the
    /// first item whose moveset has the move (combat::prepareStand()).
    std::string WeaponId;
    bool WithDummy = false;   ///< The stand's dummy to hit; otherwise it stands far away.
};

/// One physics step of the move.
struct FactFrame {
    /// The attack's clip time after the step, s.
    float ClipSec = 0.0f;
    combat::AttackPhase Phase = combat::AttackPhase::None;
    /// The fighter and the other one (the dummy, or the far opponent), moved
    /// along the floor so that the fighter's pelvis at the start of the move
    /// is at x = 0, where the ghost's pelvis is.
    combat::FighterView Attacker;
    combat::FighterView Dummy;
};

class PhysicsPreview {
public:
    explicit PhysicsPreview(PreviewSettings NewSettings);
    ~PhysicsPreview();
    PhysicsPreview(PhysicsPreview&& Other) noexcept;
    PhysicsPreview& operator=(PhysicsPreview&& Other) noexcept;

    /// The clip to play, with its unsaved edits. If its text differs from
    /// the last one, the cache is dropped; returns whether it was.
    bool setClip(const anim::Clip& Edited);
    /// Switches the dummy; a change drops the cache. Returns whether it did.
    bool setWithDummy(bool Enabled);

    /// The frame of the fact shown at clip time \p ClipSec: the first one
    /// whose clip time reached it, else the last of the move; frames are
    /// simulated as far as needed. Nullopt if the move cannot run
    /// (getProblem()) or has no frame.
    std::optional<size_t> findFrame(float ClipSec);
    /// Frame \p Index, simulated if needed; nullptr if the move ended before
    /// it or cannot run.
    const FactFrame* getFrame(size_t Index);

    /// Why the fact cannot be shown (no move plays the clip, the clip is
    /// broken, the move did not start...); empty if it can.
    const std::string& getProblem() const { return Problem; }
    /// The stand the move runs on; nullptr if no move plays the clip.
    const combat::StandSetup* getSetup() const { return Setup ? &*Setup : nullptr; }
    bool isWithDummy() const { return Settings.WithDummy; }
    /// The move has ended: every frame is in the cache.
    bool isComplete() const { return Complete; }
    size_t getFrameCount() const { return Frames.size(); }
    /// How many times the run started from the beginning (a dropped cache
    /// and the next request).
    size_t getRunCount() const { return RunCount; }
    static constexpr double StepSec = 1.0 / 60.0;

private:
    /// Resolves the move and the stand for the settings; sets Problem.
    void prepare();
    /// Drops the frames and the battle.
    void invalidate();
    /// Simulates until one more frame is kept or the move ends; false if it
    /// ended (or cannot run).
    bool computeNext();

    PreviewSettings Settings;
    std::optional<combat::StandSetup> Setup;
    /// The distance between the pelvises at the start at which the move
    /// plays the edited clip, m, when the stand's distance chooses its other
    /// clip (MoveDef::getClip()); nullopt: the stand's distance.
    std::optional<float> DistanceM;
    std::string ClipText;     ///< writeClip() of the clip set last; empty: none.
    std::string Problem;
    std::string SetupProblem; ///< Of prepare(): stays until the settings change.

    std::unique_ptr<combat::Battle> Fight;
    std::optional<combat::MoveRun> Run;
    double ElapsedSec = 0.0;  ///< Of the run, the settling included.
    float OriginX = 0.0f;     ///< The fighter's pelvis when the move began, m.
    std::vector<FactFrame> Frames;
    bool Complete = false;
    size_t RunCount = 0;
};

/// How far the fact is from the target for one joint.
struct JointError {
    BodyPart Part = BodyPart::Torso;   ///< The joint, named by its child part.
    float ErrorRad = 0.0f;             ///< Fact minus target, rad, in (-pi, pi].
};

/// The joint angles (each part to its parent) of the fact \p Fact against the
/// target \p Target posed by the same rig \p Def, for the joints of the
/// upper body (torso, head and arms; not the pelvis lean and the legs, which
/// combat moves on its own), in the order of the rig's joints.
std::vector<JointError> compareToTarget(const rig::RigDef& Def, const PosedBody& Target,
                                        const combat::FighterView& Fact);

/// The joint of \p Errors with the greatest error, or nullptr.
const JointError* findWorstJoint(const std::vector<JointError>& Errors);

} // namespace fighter::editor
