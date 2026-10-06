//===- combat/leg_step.hpp - Real steps into an action's legs ---*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares LegStep: the legs moving from where they rest into the
/// leg pose of an action that needs them (a kick, the crouch, the low
/// block) with real steps, not a slide.
///
/// The legs rest where a walk stopped (combat/leg_cycle.hpp) or in a stance
/// an earlier action left them in; an action whose clip poses the legs
/// (anim::usesLegs) wants them in its own pose: the stance (or the crouch)
/// with the clip's legs over it. A foot that stands more than
/// LegStepTuning::MinDistance away from where the action puts it, or is in
/// the air, steps: it lifts, travels on an arc LegStepTuning::LiftHeight
/// high in the middle and lands at its target, while the other foot stays
/// planted where it stood. The steps go one after the other, so that one
/// foot always stands: first a foot caught in the air (by an action started
/// mid-stride) lands where the action's start pose has it, then the
/// supporting foot steps, and last the foot the clip itself moves (the
/// kicking one) steps into the clip's pose of that moment. The pelvis does
/// not move: the step
/// works on the legs only (rig::Rig::reachFoot), its height alone goes
/// from the resting legs' to the action's over the steps.
///
/// The steps take a share of the action's startup (LegStepTuning), so the
/// strike comes on the same tick as from the stance; an action without a
/// startup (crouch) takes LegStepTuning::Sec. A step shorter than that is
/// still a lifted step, just faster.
///
/// The positions are as for a fighter facing right, relative to the floor
/// point under the pelvis (rig::FootPlacement): a step keeps its meaning
/// when the pelvis is pushed meanwhile; the rig holds a planted foot where
/// it is in the world anyway.
///
/// The file is internal to the combat module.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "combat/tuning.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "rig/rig.hpp"

namespace fighter::combat {

/// One foot's step within a LegStep.
struct FootStep {
    BodyPart Foot = BodyPart::FootL;
    float BeginSec = 0.0f;       ///< Since the LegStep began, real time.
    float EndSec = 0.0f;
    rig::FootPlacement From;     ///< Where the step starts.
    /// Where it lands: a fixed place (a foot caught in the air lands where
    /// the action's start pose has it), or, without one, where the action
    /// has the foot at the moment.
    std::optional<rig::FootPlacement> To;
};

/// The most steps a LegStep makes: a foot lands, the other one steps, the
/// first one steps into the clip.
inline constexpr size_t MaxFootSteps = 3;

class LegStep {
public:
    LegStep() = default;

    /// Plans the steps from the legs as they stand (\p Now) into the
    /// action's leg pose at its start (\p Target), over \p DurationSec.
    /// \p ClipLeftLeg, \p ClipRightLeg: the action's clip poses that leg (it
    /// moves on its own afterwards). No foot to move: the plan is empty
    /// (isActive() is false) and the action takes the legs as it is.
    static LegStep plan(const rig::LegStance& Now, const rig::LegStance& Target, bool ClipLeftLeg, bool ClipRightLeg,
                        float DurationSec, const LegStepTuning& Tuning);

    /// Are the steps going on?
    bool isActive() const { return Active; }
    /// Advances the steps by \p Dt; they end at the planned duration. \p Now:
    /// how the legs stand now. A planted foot that does not step stays where
    /// it stands in it, and a step that begins starts there (the pelvis may
    /// have moved on since the plan: a walk slowing down, a push), so the
    /// rig holds the planted feet with no offset to make up. A foot that
    /// landed and is not planted yet stays where its step put it.
    void advance(float Dt, const rig::LegStance& Now);
    /// Drops the steps (the action ended).
    void cancel() { Active = false; }

    /// Puts the legs of \p Angles (the action's pose now, as for
    /// rig::Rig::setTargetAngles) where the steps have them: a foot that
    /// waits for its step stays where it stood, a stepping one is on its
    /// arc, a foot that stepped is where the action puts it now. Leaves
    /// \p Angles as they are when the steps are over.
    void apply(PerBodyPart<float>& Angles, const rig::Rig& Body) const;

    /// Where the steps have the feet for the action's leg stance \p Target
    /// now (what apply() reaches by IK), and the pelvis height.
    rig::LegStance getStance(const rig::LegStance& Target) const;

    /// The foot that steps now, if any.
    std::optional<BodyPart> getSteppingFoot() const;
    /// How far the stepping foot is in its step, 0..1 (0 without one).
    float getStepProgress() const;
    /// The planned steps, in order.
    const std::array<std::optional<FootStep>, MaxFootSteps>& getSteps() const { return Steps; }
    float getDurationSec() const { return DurationSec; }
    float getElapsedSec() const { return ElapsedSec; }

    /// "step FootR 0.40, then FootL": for the debug panel.
    std::string describe() const;
    /// Draws the arc of the stepping foot and its landing target for the
    /// action's leg stance \p Target (TargetPose category). Does nothing in
    /// the release build.
    void drawDebug(const rig::LegStance& Target, const rig::Rig& Body) const;

private:
    /// The step going on now, if any.
    const FootStep* findCurrentStep() const;
    /// Where \p Foot is at \p TimeSec for the action's leg stance \p Target.
    rig::FootPlacement placeFoot(BodyPart Foot, float TimeSec, const rig::LegStance& Target) const;
    /// Where \p Step has its foot at \p Share (0..1) of it, towards \p To.
    rig::FootPlacement placeOnArc(const FootStep& Step, const rig::FootPlacement& To, float Share) const;

    std::array<std::optional<FootStep>, MaxFootSteps> Steps;
    rig::LegStance Start;
    /// Where the feet that do not step stand: as planted at the last
    /// advance(), or where their step put them.
    rig::LegStance Standing;
    float DurationSec = 0.0f;
    float ElapsedSec = 0.0f;
    float LiftHeight = 0.0f;
    bool Active = false;
};

} // namespace fighter::combat
