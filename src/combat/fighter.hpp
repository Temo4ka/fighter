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
///       next state; attack buttons start the move the fighter's moveset
///       gives for them and the direction held (MoveLibrary::findMove()).
///       Block wins over attacking, attacking over crouching, crouching
///       over walking. Crouched, the fighter walks slowly with bent knees
///       (the crouch_walk clip) and may strike with a move mapped to a
///       downward direction (Down+Kick: the low kick) or
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
/// The pose is two layers (anim/layers.hpp): the legs and the upper body.
/// The upper body plays the stance and over it the clip on top (guard,
/// punches, blocks, reactions); the legs play the walk, rest where it
/// stopped, or play the clip on top if that clip poses the legs (a leg
/// action: kicks, the crouch, the low block).
///
/// The stride follows the press (combat/leg_cycle.hpp): the walk cycle
/// runs with the pelvis travel, so a short press is a short step. Released,
/// the legs come back into the stance around the foot that came down last
/// (the swing foot of the step going on is set down where it is): that foot
/// stays, the pelvis glides to its place in the stance over it
/// (CombatTuning::StopSettleSpeed), and the other foot steps to its place
/// (a LegStep). The pelvis does not push into the opponent for it: held,
/// the stance stays off-center. The legs rest there, feet planted, until
/// something needs them; walking again starts from the stance. A planted
/// foot left further than legStep.restepDistance from the
/// rest pose (a push, a knockback) steps there again. An action that does not pose the legs (a punch, the
/// upper blocks, a reaction) leaves them as they are. A leg action takes
/// them with real steps during its startup (combat/leg_step.hpp), and with
/// the right foot in front it plays mirrored, left leg for right
/// (CombatTuning::LegStep, "stanceAfterStop"). After it the legs rest in
/// the stance with the foot in front the action left.
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
#include "combat/leg_step.hpp"
#include "combat/moves.hpp"
#include "combat/moveset.hpp"
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
    MoveLibrary Moves;
    ClipLibrary Clips;
    CombatTuning Tuning;
    ReactionTable Reactions;
    /// The arena walls keep a standing pelvis within +-PelvisLimitX, m.
    float PelvisLimitX = 0.0f;
};

/// What a fighter knows about the battle around it in one step.
struct Surroundings {
    float OpponentX = 0.0f;   ///< The opponent's pelvis, m.
    /// The opponent lies on the floor (a ragdoll, also knocked out).
    bool OpponentDown = false;
};

/// What the last hit taken did; for the debug panel.
struct HitRecord {
    std::string MoveId;
    BodyPart Part = BodyPart::Torso;
    HitOutcome Outcome;
    bool OnShield = false;   ///< It landed on the shield the fighter holds.
};

class Fighter {
public:
    /// \p Rules must outlive the fighter. \p StartHp: see
    /// FighterConfig::StartHp. \p Gear gives the weapon (main hand) and the
    /// moveset (the items in both hands, MoveLibrary::selectSet()).
    Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const BattleRules& Rules,
            const stats::PhysicalProfile& NewProfile, const stats::Loadout& Gear, const rig::RigSetup& Setup,
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
    /// After stopAtContact() of both fighters: the posed limbs that are
    /// still too deep in the opponent (its limbs stopped too, maybe back
    /// into these) go back once more (rig::Rig::holdLimbsBack).
    void holdLimbsBack();
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
    /// The block of the fighter's moveset (with its parents and the general
    /// rules of reactions.json).
    const BlockRules& getBlock() const { return Block; }
    /// The clip of the guard the fighter holds (or would hold) now: its
    /// block's clip for Guard.
    const anim::Clip& getBlockClip() const;
    /// Back against the arena wall behind it (O.11): cannot retreat.
    bool isAgainstWall() const;
    /// Where the opponent is; the fighter turns that way when it is free to.
    bool getDesiredFacingRight() const { return DesiredFacingRight; }
    const std::optional<HitRecord>& getLastHit() const { return LastHit; }
    /// The foot in front while the legs rest (or the walk heads to rest):
    /// FootL or FootR.
    BodyPart getRestFrontFoot() const { return Walk.isEngaged() ? Walk.getFrontFoot() : RestFront; }
    /// Released the move key: the walk cycle plays on to both feet down.
    bool isStopping() const { return Walk.isStopping() || CrouchWalk.isStopping(); }
    bool isCrouchWalking() const { return State == FighterState::Crouching && CrouchWalk.isPlaying(); }
    /// A strike pressed while crouched waits until the fighter stands up.
    bool isStandingUp() const { return PendingAttack != nullptr; }
    /// The legs step into the pose of the action on top (LegStep).
    const LegStep& getLegStep() const { return Step; }
    /// The leg action on top plays with the legs swapped (the right foot was
    /// in front when it started).
    bool isLegActionMirrored() const { return LegsMirrored; }

    void fillView(FighterView& View) const;
    /// The panel lines of this fighter ("<Name> stamina" ...) and the Block
    /// zone. Does nothing in the release build.
    void drawDebug(std::string_view Name) const;

private:
    /// How deep a posed limb may press into the opponent before it stops
    /// (stopAtContact()), m.
    float getStopDepth() const;
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
    void startMove(const MoveDef& Next, const Surroundings& Around, int ChainPosition);
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
    /// or held in place): plays on to both feet down and rests there; the
    /// planted feet stay where they stand.
    void stopLegs(LegCycle& Cycle, bool Crouched, float Dt);
    /// Chooses where a stop of \p Cycle heads: the support span where the
    /// cycle has the planted feet closest to where they stand.
    void chooseStop(LegCycle& Cycle, bool Crouched);
    /// The pose of the walk (or the crouch walk) at \p TimeSec over the stance
    /// (or the crouch).
    anim::Pose getCyclePose(bool Crouched, float TimeSec) const;
    /// The pose for the motors this step: the upper layer (stance, the clip
    /// on top) and the leg layer (the walk or its rest pose, or the leg
    /// action with its steps); and the same without the step's travel
    /// (rig::Rig::setTravelPose). \p TopChanged: the clip on top started in
    /// this step (a leg action plans its steps then).
    struct TargetPoses {
        anim::Pose Moving;
        anim::Pose Still;
    };
    TargetPoses buildTargetPose(const anim::Clip* Top, bool TopChanged, float Dt);
    /// The leg pose the legs rest in when no clip poses them, and the same
    /// without the step's travel: the walk cycle once it has played (it
    /// rests at the phase where it stopped), else the stance with RestFront
    /// in front. Changes cross over (LegFade).
    void updateRestLegs(bool LegAction, float Dt);
    /// Plans the real steps into the leg action \p Top whose pose this step
    /// is \p Target (LegStep).
    void startLegStep(const anim::Clip& Top, const anim::Pose& Target);
    /// \p Authored as this fighter plays it now: mirrored (left leg for
    /// right) when it poses the legs and LegsMirrored is set.
    const anim::Clip& getPlayed(const anim::Clip& Authored) const;
    /// Should a leg action that starts now play mirrored: the right foot is
    /// in front and the tuning says "mirror"?
    bool shouldMirrorLegs() const;
    /// The stance legs with \p FrontFoot in front (mirrored for FootR).
    anim::Pose getStanceLegs(BodyPart FrontFoot) const;
    /// \name The end of a step when the move key is released
    /// @{
    /// The move key released while walking: the stance comes back around
    /// the foot that came down last, the swing foot of the step going on set
    /// down where it is (StanceSettle).
    void beginSettle();
    /// One step of the settle: the pelvis glides on to its place over the
    /// foot that stays (planWalking()), the other foot steps to its place in
    /// the stance (RestStep). Ends there, or where the pelvis cannot go on
    /// (the opponent, a wall, a push).
    void settleLegs(float Dt);
    /// The feet of the settle (RestLanding): the staying foot where it
    /// stands, the other where the stance has it with the pelvis at its
    /// place over the staying one (short of a wall, and of the opponent as
    /// close as the spacing lets it); and how far the pelvis has to go
    /// (StanceSettle::Left).
    void placeSettleFeet();
    /// The feet of a walk step along the floor (StrideAnchor): the standing
    /// foot where it stood, the swing foot with the pelvis travel from where
    /// it was to where the clip lands it, moving only while the clip has it
    /// in the air. \p Legs: the clip's legs at \p TimeSec; \p PelvisX: where
    /// the pelvis is for this pose (WalkOdometer, m). Returns \p Legs
    /// bent to put the ankles there (rig::Rig::reachFoot).
    anim::Pose placeStepFeet(const anim::Pose& Legs, float TimeSec, float PelvisX) const;
    /// Takes the feet as they stand as the anchor of the step the walk is in
    /// now (a new step, a turn of the walk, walking again).
    void anchorStep();
    /// Can the leg of \p Foot in \p Legs (with the pelvis \p PelvisHeight up)
    /// stand at \p Place: reached, and with \p KneeCap the knee bent at most
    /// rig::ControlParams::KneeExtraBend deeper than in \p Legs?
    bool canStandAt(const anim::Pose& Legs, float PelvisHeight, BodyPart Foot, const rig::FootPlacement& Place,
                    bool KneeCap) const;
    /// \p Legs (the cycle's pose) with the feet of the rest (RestLanding).
    anim::Pose applyLanding(const anim::Pose& Legs) const;
    /// Walking again from the rest: the landing and any re-step fade into
    /// the walk cycle.
    void leaveRest();
    /// Drops the rest pose's landing, a re-step and a settle (another clip
    /// took the legs, or the fighter fell).
    void clearRest();
    /// Resting: re-steps a planted foot left off the rest pose \p Target
    /// (the motor pose of this step), and advances and applies the landing
    /// or re-step going on.
    void updateRestStep(TargetPoses& Target, float Dt);
    /// @}
    std::string describeLegs() const;
    /// "step 0.12 of 0.48 m", "settle around FootL, pelvis 0.08 m to go",
    /// "rest; re-steps 2, last FootR 0.07 m".
    std::string describeStride() const;
    /// The clip on the upper body ("jab 0.12/0.44 s ...", "stance").
    std::string describeUpper() const;
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
    /// The clip on top as played (getPlayed()), or nullptr.
    const anim::Clip* getTopClip() const;
    /// The time the clip on top is posed at: the attack's or the state's;
    /// recovering from a contact, back from the contact to the clip's start.
    float getTopClipTime() const;
    void spendStamina(float Amount);
    void react(ReactionLevel Level, float Impulse, float Direction, Vec2 Point);

    rig::Rig Body;
    const BattleRules* Rules = nullptr;
    stats::PhysicalProfile Profile;
    std::optional<stats::WeaponProps> Weapon;
    const MoveSet* Set = nullptr;          ///< From the weapon: which input starts which move, and the block.
    BlockRules Block;                      ///< The block of Set, with its parents and reactions.json.
    float Hp = 0.0f;
    float Stamina = 0.0f;
    bool Exhausted = false;
    bool ExhaustedNotice = false;
    float Buildup = 0.0f;

    FighterState State = FighterState::Idle;
    float StateSec = 0.0f;                 ///< Time in the current state (clip time of crouch, block, reaction).
    PlayerCommands PreviousCmd;            ///< For the presses that request a chain.
    bool DesiredFacingRight = true;

    LegCycle Walk;                         ///< Walking, stopping and the phase the legs rest at.
    LegCycle CrouchWalk;                   ///< Walking crouched; held while Crouching.
    /// The resting legs cross over (legs only) when they change between the
    /// stance and the walk cycle.
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
    bool OpponentDown = false;             ///< Surroundings::OpponentDown of the last control().
    float OpponentGap = 1e9f;              ///< Between the pelvises in the last control(), m.
    float OpponentX = 0.0f;                ///< Surroundings::OpponentX of the last control(), m.
    bool WalkHeld = false;                 ///< The opponent slowed the walk when it last walked.
    float LastPlannedTravel = 0.0f;        ///< The controller's planned travel of the last step, m.
    /// What sets the legs when no clip poses them.
    enum class LegSource : uint8_t { Stance, Walk };
    LegSource ShownSource = LegSource::Stance; ///< Last step's; a change crosses over (LegFade).
    /// The foot in front of the stance legs when the walk cycle does not set
    /// them (the leg action before left them so).
    BodyPart RestFront = BodyPart::FootL;
    bool ShowsCrouchWalk = false;          ///< The crouch walk set the legs last step.
    bool FeetSettling = false;             ///< A walk stopped: the feet are kept where they land.
    LegStep Step;                          ///< The real steps into the leg action on top.
    /// The leg action on top took the legs by steps: its legs are shown as
    /// they are (the fade of the pose leaves them alone).
    bool LegsStepped = false;
    bool LegsMirrored = false;             ///< The leg action on top plays with the legs swapped.
    rig::LegStance LegTarget;              ///< Where the leg action has the legs this step (for the debug draw).

    /// Where the feet stand while the legs rest in the stance, when not
    /// where the stance has them: the settle after a walk going on, or a
    /// pelvis it could not take to its place (the pelvis height is the
    /// stance's). SetDown: the swing foot of the step the walk stopped in.
    struct FootLanding {
        std::optional<BodyPart> SetDown;
        rig::LegStance Feet;
    };
    std::optional<FootLanding> RestLanding;
    LegStep RestStep;                      ///< The steps of a settle, or a re-step.
    bool RestStepFresh = false;            ///< RestStep was planned in this step: it starts in the next.
    rig::LegStance RestTarget;             ///< Where RestStep takes the legs (for the debug draw).
    /// The walk released: the foot that came down last stays, the pelvis and
    /// the other foot come to their places in the stance around it.
    struct StanceSettle {
        BodyPart Stays = BodyPart::FootL;
        float StaysX = 0.0f;    ///< Where its ankle is (lands) along the arena, m.
        float StaysAt = 0.0f;   ///< Its ankle in the stance, from the pelvis (facing right), m.
        float Spread = 0.0f;    ///< The other ankle from it in the stance (facing right), m.
        float Left = 0.0f;      ///< How far the pelvis still has to go (facing right), m.
        float Sec = 0.0f;       ///< Since it began, s.
        float StuckSec = 0.0f;  ///< The steps are over and the pelvis does not get on, s.
    };
    std::optional<StanceSettle> Settle;
    float SettleDone = 0.0f;               ///< How far the last (or current) settle took the pelvis, m.
    float StepTravel = 0.0f;               ///< Pelvis travel in the step going on, m.
    float StepLength = 0.0f;               ///< The step going on at full stride, m.
    int Resteps = 0;                       ///< Re-steps so far, for the debug panel.
    /// LegFade runs over pelvis travel (m), not time: leaving a rest.
    bool LegFadeByTravel = false;
    /// How far the walk has taken the pelvis along the facing, m: what the
    /// walk cycle followed (not pushes, which the planted feet go along with).
    /// The feet of a step are placed against it.
    float WalkOdometer = 0.0f;
    /// Where the feet of the walk step going on are along the floor (against
    /// WalkOdometer, m): placeStepFeet().
    struct StrideAnchor {
        size_t Step = 0;           ///< LegCycle::getSteps() index.
        float Heading = 1.0f;      ///< +1 the walk goes on to the step's end, -1 back to its begin.
        float Share = 0.0f;        ///< The share of the step when anchored (LegCycle::getStepShare).
        float PelvisX = 0.0f;      ///< The pelvis then.
        float SwingX = 0.0f;       ///< The swing foot's ankle then.
        float StandX = 0.0f;       ///< The standing foot's ankle (it stays).
        bool FacingRight = true;   ///< A turn starts the step anew.
    };
    std::optional<StrideAnchor> Anchor;
    std::string LastRestep;                ///< The last one, for the debug panel.
    bool AttackFromCrouch = false;         ///< The attack (a low kick) started crouched: the crouch stays below it.
    const MoveDef* PendingAttack = nullptr; ///< Pressed while crouched: starts once the fighter stood up.
    float StandUpLeftSec = 0.0f;

    BlockZone Guard = BlockZone::Mid;      ///< Meaningful while Blocking.

    const MoveDef* Move = nullptr;         ///< The move being performed while Attacking.
    const anim::Clip* AttackClip = nullptr;
    float AttackTime = 0.0f;               ///< Clip time, s.
    float AttackTimeBefore = 0.0f;         ///< Clip time before the last step, s.
    ContactStage Contact = ContactStage::None;
    float ContactHoldLeftSec = 0.0f;       ///< While Holding, real time.
    bool Jammed = false;                   ///< Stopped at the opponent in the startup.
    float ContactClipSec = 0.0f;           ///< The clip time the strike stopped at the opponent.
    float AttackRate = 1.0f;               ///< Clip seconds per second.
    bool AttackLanded = false;
    bool AttackHitClean = false;
    float RecoverySec = 0.0f;              ///< Real time since the active phase ended.
    int ChainLength = 0;                   ///< Strikes in the current chain, this one included.
    const MoveDef* ChainRequest = nullptr;

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
