//===- rig/contact.hpp - Contact of two fighters, one entry -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares rig::ContactResolver, the one place that keeps two
/// fighters from passing through each other (docs/ARCHITECTURE.md, "Контакт
/// двух бойцов"). The battle owns one and calls its stages in this order
/// every step:
///  1. beforeStep(), after both rigs planned their pelvis motion and before
///     Rig::applyControl: the walls, a fighter lying on the floor and the
///     spacing of two standing fighters correct the planned pelvis motion
///     (rig/spacing.hpp: the pelvises and the predicted bodies, posed and
///     carried). Whatever moves a pelvis by plan (the walk, knockback, the
///     push-out) is corrected here alike: a new source of planned pelvis
///     motion (a lunge keyed in a clip) adds to PelvisController::plan and
///     follows the share of its travel the pelvis makes
///     (PelvisController::getTravelShare, Rig::setTravelPose), as the walk
///     does;
///  2. the physics step: Box2D keeps the physical parts (torso, head, arms)
///     off everything; a stuck arm yields (Rig::applyControl, before the
///     step: it needs the clip's pose of the step);
///  3. afterStep(), after the physics step: the posed limbs (the legs) of
///     both fighters go back to the contact along their own motion
///     (Rig::stopPosedLimbs): the strikers of an attack at a touch, every
///     limb that went deeper than the stop depth. Both fighters are
///     resolved together: each was stopped against the other's limbs as
///     they were then, so while the one stopped last moved a limb back
///     (maybe into the other's), the other one is stopped again, in turn,
///     until a pass moves nothing (at most MaxPosedPasses passes of one
///     fighter);
///  4. onStrikeLanded(), for every landed strike: a hit at close range
///     pushes the fighters apart (rig::pushApartOnHit).
/// findWorstOverlap() checks the result: the overlap of two parts of
/// different fighters beyond the tolerance of their pair.
///
/// drawDebug() shows what each stage did in the last step, one panel line
/// per stage ("contact walls", "contact spacing", "contact posed",
/// "contact push-out") and the invariant ("overlap").
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "physics/events.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/spacing.hpp"

namespace fighter::rig {

/// The battle's contact parameters (data/combat.json and the arena).
struct ContactParams {
    SpacingParams Spacing;
    /// How deep a posed limb may press into a standing opponent before it
    /// goes back, m (contactStopDepth); a lying one it only touches.
    float StopDepth = 0.01f;
    /// How deep two parts of different fighters may overlap: two arms
    /// (armOverlapTolerance), any other pair (overlapTolerance), m.
    float ArmOverlapTolerance = 0.02f;
    float OverlapTolerance = 0.012f;
};

/// Keeps two fighters from passing through each other: the stages of a
/// simulation step (see the file comment). The fighters are given in the
/// same order to every stage.
class ContactResolver {
public:
    /// The most passes of afterStep() (a pass: one fighter's limbs).
    static constexpr int MaxPosedPasses = 8;

    ContactResolver() : ContactResolver(ContactParams{}) {}
    explicit ContactResolver(const ContactParams& Settings)
        : Params(Settings), StopDepths{Settings.StopDepth, Settings.StopDepth} {}

    /// Stage 1: corrects the planned pelvis motion of both for the walls and
    /// for each other (keepApart()). Call after Rig::planMotion of both and
    /// before Rig::applyControl.
    void beforeStep(Rig& First, Rig& Second, float Dt);
    /// Stage 3: the posed limbs of both back to the contact. Call after the
    /// physics step. Returns what the first pass did to each fighter's
    /// strikers and every limb held back (in the order given).
    std::array<PosedStop, 2> afterStep(Rig& First, Rig& Second);
    /// Stage 4: a strike of \p Attacker landed on \p Victim: a hit at close
    /// range pushes them apart (pushApartOnHit()). Returns that distance, m.
    float onStrikeLanded(Rig& Attacker, Rig& Victim);

    /// How deep \p Overlap of two parts of different fighters may be, m.
    float getOverlapTolerance(const physics::PartOverlap& Overlap) const;
    /// The overlap of two parts of different fighters in \p PhysWorld that is
    /// furthest beyond its tolerance (not necessarily the deepest), or
    /// nullopt if none overlap.
    std::optional<physics::PartOverlap> findWorstOverlap(const physics::World& PhysWorld) const;

    /// How deep the posed limbs of fighter \p Index (0: the first one given
    /// to the stages) may press into the other one in this step, m: the
    /// stop depth, none into a fighter lying on the floor (as of
    /// beforeStep()).
    float getStopDepth(size_t Index) const { return StopDepths[Index]; }
    /// The passes afterStep() made in the last step (2 for a step in which
    /// the second fighter moved nothing back; 0 before any).
    int getPosedPasses() const { return PosedPasses; }

    /// The panel lines of the stages and the overlap (the worst so far, kept
    /// with \p TimeSec, s), and the overlap mark. \p Names: the fighters'
    /// names for the panel, in the order given to the stages. Does nothing
    /// in the release build.
    void drawDebug(const physics::World& PhysWorld, double TimeSec, const std::array<const char*, 2>& Names);

private:
    ContactParams Params;
    /// What the stages did in the last step, for the panel.
    SpacingReport Spacing;
    std::array<float, 2> WallShift{};
    std::array<int, 2> WallSide{};
    std::array<PosedStop, 2> Posed;
    std::array<std::bitset<BodyPartCount>, 2> Held;
    int PosedPasses = 0;
    /// How deep each one's posed limbs may press into the other this step,
    /// m: none into a fighter lying on the floor (beforeStep()).
    std::array<float, 2> StopDepths{};
    std::string PushOut = "-";
    /// The worst overlap seen so far and when (drawDebug()).
    std::optional<physics::PartOverlap> WorstOverlap;
    double WorstOverlapSec = 0.0;
};

} // namespace fighter::rig
