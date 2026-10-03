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
/// PLACEHOLDER until the state machine of agent D (task 2.3): commands map
/// straight to clips (move -> walk cycle, jab -> jab, body kick -> kick) over
/// the stance. A knocked-down fighter ignores commands until it stands.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
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
    /// \p NewClips must outlive the fighter. \p StartHp: see
    /// FighterConfig::StartHp.
    Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const ClipSet& NewClips,
            const stats::PhysicalProfile& NewProfile, const rig::RigSetup& Setup, std::optional<float> StartHp);

    /// Chooses the clips for \p Cmd, sets the rig targets and plans the
    /// pelvis motion. Call once per step, before applyControl(). Returns true
    /// if an attack started (getMoveId() names it).
    bool control(const PlayerCommands& Cmd, float Dt);
    /// Moves the body for the next physics step (after the battle corrected
    /// the planned pelvis motion).
    void applyControl(float Dt);

    /// This fighter was hit; \p Direction is +1 if the hit pushes it to the
    /// right, -1 to the left.
    void onHit(const physics::HitEvent& Hit, float Direction);

    rig::Rig& getRig() { return Body; }
    const rig::Rig& getRig() const { return Body; }
    const stats::PhysicalProfile& getProfile() const { return Profile; }
    float getHp() const { return Hp; }
    /// The move being performed (data/moves/<id>.json), empty if none.
    std::string_view getMoveId() const { return Attack ? MoveId : std::string_view(); }
    /// Name of the clip on top (attack, walk or stance) and its time, s.
    std::string_view getClipName() const;
    float getClipTime() const;
    /// Is an attack in its striking phase?
    bool isAttackActive() const;
    /// Is \p Part a striking part of an attack in its striking phase that
    /// has not landed yet? Only such contacts are hits; the rest are bumps.
    bool isStrikingWith(BodyPart Part) const;
    /// The current attack hit: it hits only once, later contacts are bumps.
    void onStrikeLanded() { AttackLanded = true; }

    void fillView(FighterView& View) const;

private:
    void startAttack(const anim::Clip& NewAttack, std::string_view NewMoveId);

    rig::Rig Body;
    const ClipSet* Clips = nullptr;
    stats::PhysicalProfile Profile;
    float Hp = 0.0f;

    bool Walking = false;                  ///< The walk cycle plays (also while finishing a step).
    float WalkTime = 0.0f;                 ///< Phase of the walk cycle, s.
    float WalkDirection = 1.0f;            ///< +1 forwards, -1 backwards (the cycle runs in reverse).
    const anim::Clip* Attack = nullptr;    ///< The attack being played, if any.
    std::string_view MoveId;               ///< Its move id; points to a literal.
    float AttackTime = 0.0f;
    bool AttackLanded = false;
};

} // namespace fighter::combat
