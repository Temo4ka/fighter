//===- combat/fighter.hpp - One fighter of a battle -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares combat::Fighter, one fighter inside a Battle: its
/// physical body (rig::Rig), its state machine, health, stamina and the
/// buildup of hits. It is internal to the combat module; the public API is
/// Battle.
///
/// The state machine (FighterState in the snapshot) decides who may do what:
///
///   Idle, Walking, Crouching, Blocking -- free: the commands choose the
///       next state; an attack button starts the move findMove() gives for
///       the button and the weapon held. Block wins over attacking, attacking
///       over crouching, crouching over walking.
///   Attacking -- the move's clip plays (startup, active, recovery); walking
///       only if the clip allows it. A clean hit may cancel the recovery into
///       a move of MoveDef::ChainTo (a short chain). Back to free at the end.
///   Reacting -- a hit of level Flinch or stronger: no control for the
///       level's stun_sec; a new hit only raises the level.
///   KnockedDown, GettingUp -- the rig's ragdoll and getting up; no control.
///       A lying fighter cannot be hit (no juggling, O.7); a fighter getting
///       up can, and a knockdown hit fells it again.
///   KnockedOut -- HP reached 0: it falls and stays down.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "anim/clip.hpp"
#include "anim/playback.hpp"
#include "anim/pose.hpp"
#include "combat/clip_library.hpp"
#include "combat/commands.hpp"
#include "combat/moves.hpp"
#include "combat/reactions.hpp"
#include "combat/snapshot.hpp"
#include "combat/tuning.hpp"
#include "physics/events.hpp"
#include "physics/world.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

namespace fighter::combat {

/// The data both fighters of a battle play by. It outlives them.
struct BattleRules {
    std::vector<MoveDef> Moves;
    ClipLibrary Clips;
    CombatTuning Tuning;
    ReactionTable Reactions;
    /// The arena walls keep a standing pelvis within +-PelvisLimitX, m.
    float PelvisLimitX = 0.0f;
};

/// What a fighter knows about the battle around it in one step.
struct Surroundings {
    float OpponentX = 0.0f;   ///< The opponent's pelvis, m.
};

/// What the last hit taken did; for the debug panel.
struct HitRecord {
    std::string MoveId;
    BodyPart Part = BodyPart::Torso;
    HitOutcome Outcome;
};

class Fighter {
public:
    /// \p Rules must outlive the fighter. \p StartHp: see
    /// FighterConfig::StartHp. \p Weapon: the loadout's weapon, if any.
    Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const BattleRules& Rules,
            const stats::PhysicalProfile& NewProfile, const stats::WeaponProps* Weapon, const rig::RigSetup& Setup,
            std::optional<float> StartHp);

    /// Runs the state machine for \p Cmd, sets the rig targets and plans the
    /// pelvis motion. Call once per step, before applyControl(). Returns the
    /// move that started in this step (also by a chain), or nullptr.
    const MoveDef* control(const PlayerCommands& Cmd, const Surroundings& Around, float Dt);
    /// Moves the body for the next physics step (after the battle corrected
    /// the planned pelvis motion).
    void applyControl(float Dt);

    /// Can a strike hit this fighter now? Not while it lies on the floor.
    bool isHittable() const;
    /// This fighter was hit by \p Move; \p PowerScale is the attacker's
    /// weapon power for a weapon move, \p Direction +1 if the hit pushes it
    /// to the right, -1 to the left. Applies damage, buildup, block stamina
    /// and the reaction (stun, knockdown, knockout) and returns what it did.
    HitOutcome takeHit(const physics::HitEvent& Hit, const MoveDef& Move, float PowerScale, float Direction);
    /// The current attack landed; it hits only once, later contacts are
    /// bumps. \p Clean: not blocked, so it may be chained.
    void onStrikeLanded(bool Clean);
    /// True once after the stamina ran out (for the Exhausted event).
    bool takeExhaustedNotice();

    rig::Rig& getRig() { return Body; }
    const rig::Rig& getRig() const { return Body; }
    const stats::PhysicalProfile& getProfile() const { return Profile; }
    FighterState getState() const { return State; }
    float getHp() const { return Hp; }
    float getStamina() const { return Stamina; }
    bool isExhausted() const { return Exhausted; }
    float getBuildup() const { return Buildup; }
    /// The move being performed, or nullptr.
    const MoveDef* getMove() const { return State == FighterState::Attacking ? Move : nullptr; }
    /// Its id, empty if none.
    std::string_view getMoveId() const;
    /// The damage multiplier of \p Attack thrown by this fighter: the weapon's
    /// power for a weapon move, otherwise 1.
    float getPowerScale(const MoveDef& Attack) const;
    /// Time of the clip on top (attack, block, reaction, walk or stance), s.
    float getClipTime() const;
    /// Clip seconds per second of the current attack (O.7): the fighter's
    /// attack speed and weapon, slowed when exhausted and, if needed, so that
    /// the active phase starts no sooner than the move's min_startup_sec.
    float getAttackRate() const { return AttackRate; }
    /// "jab 0.12/0.44 s x1.25 startup 0.13 s, active, blend 0.40": the clip
    /// on top for the debug panel.
    std::string describeClip() const;
    /// Is an attack in its striking phase?
    bool isAttackActive() const;
    /// Is \p Part a striking part of an attack in its striking phase that
    /// has not landed yet? Only such contacts are hits; the rest are bumps.
    bool isStrikingWith(BodyPart Part) const;
    /// Back against the arena wall behind it (O.11): cannot retreat.
    bool isAgainstWall() const;
    /// Where the opponent is; the fighter turns that way when it is free to.
    bool getDesiredFacingRight() const { return DesiredFacingRight; }
    const std::optional<HitRecord>& getLastHit() const { return LastHit; }

    void fillView(FighterView& View) const;
    /// The panel lines of this fighter ("<Name> stamina" ...) and the Block
    /// zone. Does nothing in the release build.
    void drawDebug(std::string_view Name) const;

private:
    bool isFree() const;
    void setState(FighterState Next);
    void updateMeters(float Dt);
    void syncPosture();
    /// Advances the attack; returns the move a chain started, or nullptr.
    const MoveDef* advanceAttack(const PlayerCommands& Cmd, const Surroundings& Around, float Dt);
    /// Chooses the free state for \p Cmd; returns the move it started, or nullptr.
    const MoveDef* chooseFreeState(const PlayerCommands& Cmd, const Surroundings& Around);
    void startMove(const MoveDef& Next, const Surroundings& Around, int ChainPosition);
    float planWalking(const PlayerCommands& Cmd, float Dt);
    const anim::Clip* getTopClip() const;
    float getTopClipTime() const;
    void spendStamina(float Amount);
    void react(ReactionLevel Level, float Impulse, float Direction, Vec2 Point);

    rig::Rig Body;
    const BattleRules* Rules = nullptr;
    stats::PhysicalProfile Profile;
    std::optional<stats::WeaponProps> Weapon;
    float Hp = 0.0f;
    float Stamina = 0.0f;
    bool Exhausted = false;
    bool ExhaustedNotice = false;
    float Buildup = 0.0f;

    FighterState State = FighterState::Idle;
    float StateSec = 0.0f;                 ///< Time in the current state (clip time of crouch, block, reaction).
    PlayerCommands PreviousCmd;            ///< For the presses that request a chain.
    bool DesiredFacingRight = true;

    bool Walking = false;                  ///< The walk cycle plays (also while finishing a step).
    float WalkTime = 0.0f;                 ///< Phase of the walk cycle, s.
    float WalkDirection = 1.0f;            ///< +1 forwards, -1 backwards (the cycle runs in reverse).

    BlockZone Guard = BlockZone::Mid;      ///< Meaningful while Blocking.

    const MoveDef* Move = nullptr;         ///< The move being performed while Attacking.
    const anim::Clip* AttackClip = nullptr;
    float AttackTime = 0.0f;               ///< Clip time, s.
    float AttackRate = 1.0f;               ///< Clip seconds per second.
    bool AttackLanded = false;
    bool AttackHitClean = false;
    float RecoverySec = 0.0f;              ///< Real time since the active phase ended.
    int ChainLength = 0;                   ///< Strikes in the current chain, this one included.
    std::optional<MoveButton> ChainRequest;

    ReactionLevel Reaction = ReactionLevel::None;   ///< While Reacting.
    float StunLeftSec = 0.0f;
    std::optional<HitRecord> LastHit;

    /// The pose fades over when the clip on top changes (anim::PoseTransition).
    anim::PoseTransition Fade;
    anim::Pose Shown;                      ///< The pose the motors got last step.
    const anim::Clip* ShownTop = nullptr;  ///< The clip on top in the last step.
    bool TopRestarted = false;             ///< The clip on top started again (a chain, a stronger reaction).
};

} // namespace fighter::combat
