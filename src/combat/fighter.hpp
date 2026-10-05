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
///       over crouching, crouching over walking. Crouched, the fighter walks
///       slowly with bent knees (the crouch_walk clip) and may low kick or
///       block low; any other strike stands it up first (Idle for
///       CombatTuning::CrouchStandUpSec), then starts. Blocking, it can only
///       step back, slowly.
///   Attacking -- the move's clip plays (startup, active, recovery); walking
///       only if the clip allows it. A clean hit may cancel the recovery into
///       a move of MoveDef::ChainTo (a short chain). Back to free at the end.
///       A posed striker (a kick) that meets the opponent's posed parts
///       (legs, pelvis) stops there: the clip holds that pose for
///       CombatTuning::ContactHoldSec, then recovers, blending from the
///       contact pose (stopAtContact).
///   Reacting -- a hit of level Flinch or stronger: no control for the
///       level's stun_sec; a new hit only raises the level.
///   KnockedDown, GettingUp -- the rig's ragdoll and getting up; no control.
///       A lying fighter cannot be hit (no juggling, O.7); a fighter getting
///       up can, and a knockdown hit fells it again.
///   KnockedOut -- HP reached 0: it falls and stays down.
///
/// Walking stops on both feet (combat/leg_cycle.hpp): released, the walk
/// cycle plays on to the nearest double-support phase and holds it, so the
/// fighter stands in the normal stance or the switched one (right foot
/// forward). From the switched stance a strike led by the left side (jab,
/// kicks) steps the legs back into the normal stance during its startup.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
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
#include "combat/leg_cycle.hpp"
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
    /// A \p JammedStrike (Fighter::isJammed) gets no min_reaction.
    HitOutcome takeHit(const physics::HitEvent& Hit, const MoveDef& Move, float PowerScale, float Direction,
                       bool JammedStrike = false);
    /// The current attack landed; it hits only once, later contacts are
    /// bumps. \p Clean: not blocked and not jammed, so it may be chained.
    void onStrikeLanded(bool Clean);
    /// Call after the physics step: a posed striking limb of an attack that
    /// ran into the opponent (its posed parts, or a part the solver could
    /// not push away) goes back to the contact (rig::Rig::stopAtContact), in
    /// any phase of the attack. The first stop of an attack holds the clip
    /// at the contact pose for CombatTuning::ContactHoldSec; then the
    /// recovery plays, blending from the contact pose over
    /// CombatTuning::ContactRecoveryBlendSec. The hit itself is the physics
    /// world's, as for any strike. A first stop in the startup jams the
    /// attack (isJammed): the leg did hit, so that contact is a strike too,
    /// with its real speed, but not a clean one (no min_reaction, no chain).
    /// The other posed limbs (the legs) are held back at the opponent too,
    /// attacking or not (rig::Rig::holdLimbsBack).
    void stopAtContact();
    /// Did the current attack run into the opponent in its startup
    /// (stopAtContact)?
    bool isJammed() const { return getMove() && Jammed; }
    /// Is the attack holding the pose of a posed strike stopped at the
    /// opponent (stopAtContact)?
    bool isHoldingContact() const { return getMove() && Contact == ContactStage::Holding; }
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
    /// attack speed and weapon, slowed when exhausted.
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
    /// The stance the legs stand in when the fighter stands still.
    StanceVariant getStanceVariant() const { return Walk.getVariant(); }
    /// Released the move key: the walk cycle plays on to both feet down.
    bool isStopping() const { return Walk.isStopping() || CrouchWalk.isStopping(); }
    bool isCrouchWalking() const { return State == FighterState::Crouching && CrouchWalk.isPlaying(); }
    /// A strike pressed while crouched waits until the fighter stands up.
    bool isStandingUp() const { return PendingAttack.has_value(); }
    /// The attack steps the legs into the normal stance during its startup
    /// (from the switched one, or from a walk).
    bool isSwitchStepping() const { return SwitchStep; }

    void fillView(FighterView& View) const;
    /// The panel lines of this fighter ("<Name> stamina" ...) and the Block
    /// zone. Does nothing in the release build.
    void drawDebug(std::string_view Name) const;

private:
    /// The strikers' part of stopAtContact().
    void stopStrikeAtContact();
    /// What a posed strike stopped at the opponent does (stopAtContact).
    enum class ContactStage : uint8_t {
        None,        ///< Not stopped in this attack.
        Holding,     ///< The clip holds the contact pose.
        Recovering,  ///< The recovery blends back from the contact pose.
    };

    bool isFree() const;
    void setState(FighterState Next);
    void updateMeters(float Dt);
    void syncPosture();
    /// Advances the attack; returns the move a chain started, or nullptr.
    const MoveDef* advanceAttack(const PlayerCommands& Cmd, const Surroundings& Around, float Dt);
    /// Chooses the free state for \p Cmd; returns the move it started, or nullptr.
    const MoveDef* chooseFreeState(const PlayerCommands& Cmd, const Surroundings& Around, float Dt);
    /// \p Cmd: the commands of this step (does the fighter walk on?).
    void startMove(const MoveDef& Next, const Surroundings& Around, const PlayerCommands& Cmd, int ChainPosition);
    /// The walking speed \p Cmd asks for, m/s (world); remembers what the
    /// legs should do for advanceLegs().
    float planWalking(const PlayerCommands& Cmd);
    /// Moves the walk cycle (or the crouch walk) after the pelvis planned its
    /// motion: walking, it advances by the planned travel and later keeps
    /// the share the pelvis really makes (applyControl()); held in place by
    /// the opponent, it stops on both feet instead of marching on the spot;
    /// pushed along without walking, the legs step with the pelvis; else it
    /// stops (on both feet).
    void advanceLegs(float Dt);
    /// The stop of the leg cycle when the fighter does not walk (released,
    /// or held in place): plays on to both feet down, the feet settle.
    void stopLegs(LegCycle& Cycle, bool Crouched, float Dt);
    /// The pose for the motors this step: stance, legs, the clip on top;
    /// and the same without the step's travel (rig::Rig::setTravelPose).
    struct TargetPoses {
        anim::Pose Moving;
        anim::Pose Still;
    };
    TargetPoses buildTargetPose(const anim::Clip* Top, float Dt);
    std::string describeLegs() const;
    /// "pose walk -> strike 0.04 s, 0.40; legs -": the blends in progress.
    std::string describeBlends() const;
    /// A blend in progress, for the debug panel.
    struct BlendInfo {
        PoseKind From = PoseKind::Stance;
        PoseKind To = PoseKind::Stance;
        float Sec = 0.0f;
    };
    /// Starts the fade of the pose for the clip on top changing to \p Top
    /// (the clip's own blend time or the blend table's).
    void beginTopFade(const anim::Clip* Top);
    /// Starts \p Transition from \p From for the change \p FromKind ->
    /// \p ToKind (the blend table's time) and records it in \p Info.
    void beginBlend(anim::PoseTransition& Transition, const anim::Pose& From, BlendInfo& Info, PoseKind FromKind,
                    PoseKind ToKind);
    PoseKind getClipKind(const anim::Clip& Source) const;
    /// The legs of the switched stance: the stance_switched clip, or the
    /// stance's legs mirrored.
    anim::Pose getSwitchedStanceLegs() const;
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

    LegCycle Walk;                         ///< Walking, stopping and the stance it held.
    LegCycle CrouchWalk;                   ///< Walking crouched; held while Crouching.
    /// The legs below the clip on top cross over (legs only) when they change
    /// between the stance and the walk cycle, and for the switch-step.
    anim::PoseTransition LegFade;
    anim::Pose ShownLegs;                  ///< What LegFade gave last step.
    anim::Pose ShownLegsStill;             ///< ShownLegs without the step's travel.
    /// What the legs do in this step (planWalking(), advanceLegs()).
    struct LegPlan {
        bool Crouched = false;
        bool WantsToMove = false;
        float Sign = 0.0f;           ///< Of the requested walk, world X.
        bool FollowsTravel = false;  ///< The cycle walked with the travel below.
        float Travel = 0.0f;         ///< The pelvis travel the cycle's step assumes, m (world).
        bool Held = false;           ///< Walking, but the opponent holds the pelvis.
        bool Pushed = false;         ///< Walking, but pushed back: the legs step backwards.
    };
    LegPlan Stride;
    float LastPlannedTravel = 0.0f;        ///< The controller's planned travel of the last step, m.
    /// What sets the legs below the clip on top.
    enum class LegSource : uint8_t { Stance, SwitchedStance, Walk };
    LegSource ShownSource = LegSource::Stance; ///< Last step's; a change crosses over (LegFade).
    bool ShowsCrouchWalk = false;          ///< The crouch walk set the legs last step.
    bool FeetSettling = false;             ///< A walk stopped: the feet are kept where they land.
    bool SwitchStep = false;               ///< The attack steps the legs into the normal stance.
    bool AttackFromCrouch = false;         ///< The attack (a low kick) started crouched: the crouch stays below it.
    std::optional<MoveButton> PendingAttack; ///< Pressed while crouched: starts once the fighter stood up.
    float StandUpLeftSec = 0.0f;

    BlockZone Guard = BlockZone::Mid;      ///< Meaningful while Blocking.

    const MoveDef* Move = nullptr;         ///< The move being performed while Attacking.
    const anim::Clip* AttackClip = nullptr;
    float AttackTime = 0.0f;               ///< Clip time, s.
    float AttackTimeBefore = 0.0f;         ///< Clip time before the last step, s.
    ContactStage Contact = ContactStage::None;
    float ContactHoldLeftSec = 0.0f;       ///< While Holding, real time.
    bool Jammed = false;                   ///< Stopped at the opponent in the startup.
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
    BlendInfo TopBlend;                    ///< The last fade of Fade.
    BlendInfo LegBlend;                    ///< The last fade of LegFade (not the switch-step).
    anim::Pose Shown;                      ///< The pose the motors got last step.
    anim::Pose ShownStill;                 ///< Shown without the step's travel (rig::Rig::setTravelPose).
    const anim::Clip* ShownTop = nullptr;  ///< The clip on top in the last step.
    bool TopRestarted = false;             ///< The clip on top started again (a chain, a stronger reaction).
};

} // namespace fighter::combat
