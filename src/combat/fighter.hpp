//===- combat/fighter.hpp - One fighter of a battle -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares combat::Fighter, one fighter inside a Battle: its
/// physical body (rig::Rig), the clips it plays and its health. It is
/// internal to the combat module; the public API is Battle.
///
/// PLACEHOLDER for the physics spike (phase 1): commands map straight to
/// clips (move -> walk cycle, punch -> jab, kick -> kick) over the stance.
/// The state machine of agent D (phase 2) replaces this controller.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include "anim/clip.hpp"
#include "anim/pose.hpp"
#include "combat/commands.hpp"
#include "combat/snapshot.hpp"
#include "physics/events.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

namespace fighter::combat {

/// The clips every fighter plays, from data/poses/.
struct ClipSet {
    anim::Clip Stance;   ///< Sets every joint: the base layer.
    anim::Clip Walk;     ///< Loop over the stance while moving.
    anim::Clip Jab;      ///< One-shot over walking or the stance.
    anim::Clip Kick;

    /// Reads stance.json, walk.json, jab.json and kick.json from \p PosesDir.
    /// Throws std::runtime_error.
    static ClipSet load(const std::filesystem::path& PosesDir);
};

class Fighter {
public:
    /// \p NewClips must outlive the fighter.
    Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const ClipSet& NewClips,
            const stats::PhysicalProfile& NewProfile, const rig::RigSetup& Setup);

    /// Chooses the clips for \p Cmd, sets the rig targets and applies the
    /// rig controller. Call once per step, before the physics step.
    void control(const PlayerCommands& Cmd, float Dt);

    /// This fighter was hit.
    void onHit(const physics::HitEvent& Hit);

    const rig::Rig& getRig() const { return Body; }
    const stats::PhysicalProfile& getProfile() const { return Profile; }
    float getHp() const { return Hp; }
    /// Name of the clip on top (attack, walk or stance) and its time, s.
    std::string_view getClipName() const;
    float getClipTime() const;
    /// Is an attack in its striking phase?
    bool isAttackActive() const;
    /// Is \p Part a striking part of an attack in its striking phase? Only
    /// such contacts are hits; the rest are bumps.
    bool isStrikingWith(BodyPart Part) const;

    void fillView(FighterView& View) const;

private:
    void startAttack(const anim::Clip& NewAttack);

    rig::Rig Body;
    const ClipSet* Clips = nullptr;
    stats::PhysicalProfile Profile;
    float Hp = 0.0f;

    bool Walking = false;                  ///< The walk cycle plays (also while finishing a step).
    float WalkTime = 0.0f;                 ///< Phase of the walk cycle, s.
    float WalkHeldSec = 0.0f;              ///< How long walking has been requested, s.
    float WalkDirection = 1.0f;            ///< +1 forwards, -1 backwards (the cycle runs in reverse).
    const anim::Clip* Attack = nullptr;    ///< The attack being played, if any.
    float AttackTime = 0.0f;
};

} // namespace fighter::combat
