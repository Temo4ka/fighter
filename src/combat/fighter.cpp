#include "combat/fighter.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include "anim/layers.hpp"
#include "debug/draw.hpp"

namespace fighter::combat {
namespace {

/// A pelvis this close to its wall limit counts as against the wall, m.
constexpr float WallContactM = 0.01f;
/// The opponent must be this far to one side before the fighter wants to
/// face the other way: no flipping while they overlap, m.
constexpr float FacingDeadZoneM = 0.05f;

/// Width and offset of the Block zone drawn in front of the body, m.
constexpr float BlockBarOffsetM = 0.3f;
constexpr float BlockBarHalfWidthM = 0.04f;

constexpr std::array AttackButtons = {MoveButton::Jab, MoveButton::HeavyPunch, MoveButton::BodyKick,
                                      MoveButton::LowKick};

std::string_view getWeaponClass(const std::optional<stats::WeaponProps>& Weapon);
bool hasUpperJoints(const anim::Clip& Source);

} // namespace

Fighter::Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const BattleRules& NewRules,
                 const stats::PhysicalProfile& NewProfile, const stats::WeaponProps* NewWeapon,
                 const rig::RigSetup& Setup, std::optional<float> StartHp)
    : Body(PhysWorld, Description, Setup), Rules(&NewRules), Profile(NewProfile),
      Hp(std::clamp(StartHp.value_or(NewProfile.MaxHp), 0.0f, NewProfile.MaxHp)), Stamina(NewProfile.MaxStamina),
      DesiredFacingRight(Setup.FacingRight) {
    if (NewWeapon) Weapon = *NewWeapon;
    Shown = anim::sampleClip(NewRules.Clips.get(clips::Stance), 0.0f);
    Body.setTargetAngles(Shown.Angles);
    Body.snapToTargets();
    const float MinSpread = NewRules.Tuning.RestMinFootSpread;
    Walk = makeLegCycle(NewRules.Clips.get(clips::Walk), Shown, Body, MinSpread);
    anim::Pose Crouched = Shown;
    anim::layerPose(Crouched, anim::sampleClip(NewRules.Clips.get(clips::Crouch), 0.0f));
    CrouchWalk = makeLegCycle(NewRules.Clips.get(clips::CrouchWalk), Crouched, Body, MinSpread);
    ShownLegs = anim::selectJoints(Shown, anim::getLegJoints());
    ShownLegsStill = ShownLegs;
}

const MoveDef* Fighter::control(const PlayerCommands& Cmd, const Surroundings& Around, float Dt) {
    updateMeters(Dt);
    syncPosture();
    OpponentDown = Around.OpponentDown;
    StateSec += Dt;

    // Where the opponent is. The body turns only when the fighter is free
    // to act: not during an attack, a reaction, on the floor or getting up.
    const float OwnX = Body.getPartPosition(BodyPart::Pelvis).X;
    if (std::abs(Around.OpponentX - OwnX) > FacingDeadZoneM) DesiredFacingRight = Around.OpponentX > OwnX;

    const MoveDef* Started = nullptr;
    if (State == FighterState::Reacting) {
        StunLeftSec -= Dt;
        if (StunLeftSec <= 0.0f) setState(FighterState::Idle);
    } else if (State == FighterState::Attacking) {
        Started = advanceAttack(Cmd, Around, Dt);
    }
    if (isFree()) {
        Started = chooseFreeState(Cmd, Around, Dt);
        // The rig mirrors the body in the next applyControl().
        if (DesiredFacingRight != Body.isFacingRight()) Body.setFacing(DesiredFacingRight);
    }

    // The pelvis plans its motion first: the legs step with its travel.
    Body.setMoveVelocity(planWalking(Cmd));
    Body.planMotion(Dt);
    advanceLegs(Dt);
    if (State == FighterState::Idle && Walk.isPlaying()) setState(FighterState::Walking);
    if (State == FighterState::Walking && !Walk.isPlaying()) setState(FighterState::Idle);

    // A crouch or a low block that starts now plays with the foot in front
    // the legs have (an attack chose in startMove()).
    if (State == FighterState::Crouching || State == FighterState::Blocking) {
        const ClipLibrary& Clips = Rules->Clips;
        const anim::Clip& Authored = State == FighterState::Crouching ? Clips.get(clips::Crouch)
                                                                      : Clips.getBlock(Guard);
        const bool Continues = ShownTop && &Clips.getAuthored(*ShownTop) == &Authored;
        if (anim::usesLegs(Authored) && !Continues) LegsMirrored = shouldMirrorLegs();
    }
    const anim::Clip* Top = getTopClip();
    const bool TopChanged = Top != ShownTop || TopRestarted;
    // A leg action ends the walk where it rested: afterwards the legs rest
    // in the stance it leaves them in.
    if (Top && anim::usesLegs(*Top)) {
        RestFront = LegsMirrored ? BodyPart::FootR : BodyPart::FootL;
        Walk.settle(RestFront);
    }
    const TargetPoses Target = buildTargetPose(Top, TopChanged, Dt);
    // A clip that starts fades in, one that ends fades out: the clip's own
    // time or the blend table's for the change.
    if (TopChanged) beginTopFade(Top);
    ShownTop = Top;
    TopRestarted = false;
    Shown = Fade.step(Target.Moving, Dt);
    ShownStill = Fade.peek(Target.Still);
    // Legs that step into the action's pose are not faded: the steps are
    // the transition.
    if (LegsStepped) {
        anim::layerPose(Shown, anim::selectJoints(Target.Moving, anim::getLegJoints()));
        anim::layerPose(ShownStill, anim::selectJoints(Target.Still, anim::getLegJoints()));
    }

    Body.setTargetAngles(Shown.Angles);
    if (Stride.FollowsTravel) Body.setTravelPose(ShownStill.Angles, Stride.Travel);
    // The strikers of an attack stop at the opponent by themselves; the
    // spacing keeps the rest of the body off it.
    const std::bitset<BodyPartCount> AttackParts = getMove() ? AttackClip->Strikers : std::bitset<BodyPartCount>{};
    const bool Striking = getMove() && Contact == ContactStage::None && AttackTime < AttackClip->ActiveEndSec;
    Body.setStrikingParts(Striking ? AttackParts : std::bitset<BodyPartCount>{}, AttackParts);
    Body.setBaseStiffness(Top ? Top->Stiffness : 1.0f);
    PreviousCmd = Cmd;
    return Started;
}

void Fighter::applyControl(float Dt) {
    // The spacing may have let the pelvis make only part of its planned
    // travel: the walk cycle keeps that share of its step, so the legs step
    // only as far as the pelvis goes (the rig poses them the same way).
    if (Stride.FollowsTravel) {
        const float Share = Body.getTravelShare();
        (Stride.Crouched ? CrouchWalk : Walk).follow(Share);
        Shown = anim::blendPoses(ShownStill, Shown, Share);
        ShownLegs = anim::blendPoses(ShownLegsStill, ShownLegs, Share);
    }
    LastPlannedTravel = Body.getController().getPlannedTravel();
    Body.applyControl(Dt);
}

bool Fighter::isHittable() const { return State != FighterState::KnockedDown && State != FighterState::KnockedOut; }

HitOutcome Fighter::takeHit(const physics::HitEvent& Hit, const MoveDef& Attack, float PowerScale, float Direction,
                            bool JammedStrike) {
    const HitInput Input{
        .Impulse = Hit.Impulse,
        .Part = Hit.Victim.Part,
        .VictimMass = Body.getTotalMass(),
        .Armor = Profile.Parts[static_cast<size_t>(Hit.Victim.Part)].Armor,
        .Poise = Profile.Poise,
        .Buildup = Buildup,
        .Guard = State == FighterState::Blocking ? std::optional(Guard) : std::nullopt,
        .MoveDamage = Attack.Damage,
        .PowerScale = PowerScale,
        .MinReaction = JammedStrike ? ReactionLevel::None : Attack.MinReaction,
    };
    const HitOutcome Outcome = resolveHit(Rules->Reactions, Input);
    Hp = std::max(0.0f, Hp - Outcome.Damage);
    Buildup += Outcome.BuildupAdded;
    if (Outcome.Blocked) spendStamina(Outcome.BlockStamina);
    LastHit = HitRecord{.MoveId = Attack.Id, .Part = Hit.Victim.Part, .Outcome = Outcome};
    react(Outcome.Reaction, Hit.Impulse, Direction, Hit.Point);
    return Outcome;
}

void Fighter::onStrikeLanded(bool Clean) {
    AttackLanded = true;
    AttackHitClean = Clean;
}

void Fighter::stopAtContact() {
    // In any phase: nothing passes through the opponent. A contact in the
    // startup jams the attack. The posed limbs are held back too.
    if (getMove()) stopStrikeAtContact();
    Body.holdLimbsBack(getStopDepth());
}

void Fighter::holdLimbsBack() { Body.holdLimbsBack(getStopDepth()); }

void Fighter::stopStrikeAtContact() {
    const CombatTuning& Tuning = Rules->Tuning;
    const bool Startup = AttackTime < AttackClip->ActiveBeginSec;
    const std::optional<float> Kept =
        Body.stopAtContact(AttackClip->Strikers, getStopDepth());
    if (!Kept || Contact != ContactStage::None || AttackTimeBefore >= AttackClip->ActiveEndSec) return;

    // The first stop: the clip goes back to the time of the contact (clip
    // time advances evenly within a step) and holds there. A contact of the
    // striking phase stays in it, so one that was too slow to be a hit can
    // still land.
    const float Stopped = AttackTimeBefore + (AttackTime - AttackTimeBefore) * *Kept;
    AttackTime = Startup ? Stopped : std::clamp(Stopped, AttackClip->ActiveBeginSec, AttackClip->ActiveEndSec);
    Contact = ContactStage::Holding;
    ContactHoldLeftSec = Tuning.ContactHoldSec;
    Jammed = Startup;
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} {} at the opponent (clip {:.2f} s)", Body.getFighterIndex() + 1,
                                    Move->Id, Startup ? "jammed in the startup" : "stopped", AttackTime));
    }
}

float Fighter::getStopDepth() const {
    // A posed limb held pressed into a body lying on the floor would clamp
    // its limp parts to the floor: the ragdoll pulled away levers them up
    // into the limb (nothing moves a posed limb out of the way). On a lying
    // opponent the limb stops at the touch.
    return OpponentDown ? 0.0f : Rules->Tuning.ContactStopDepth;
}

bool Fighter::takeExhaustedNotice() { return std::exchange(ExhaustedNotice, false); }

std::string_view Fighter::getMoveId() const {
    const MoveDef* Current = getMove();
    return Current ? std::string_view(Current->Id) : std::string_view();
}

float Fighter::getPowerScale(const MoveDef& Attack) const {
    return !Attack.Weapon.empty() && Weapon ? Weapon->PowerScale : 1.0f;
}

std::string Fighter::describeClip() const {
    const anim::Clip* Top = getTopClip();
    const anim::Clip& Playing = Top ? *Top : Rules->Clips.get(Walk.isPlaying() ? clips::Walk : clips::Stance);
    const float Rate = State == FighterState::Attacking ? AttackRate : 1.0f;
    return anim::describePlayback(Playing, getClipTime(), Rate, Fade);
}

float Fighter::getClipTime() const {
    if (getTopClip()) return getTopClipTime();
    return Walk.isPlaying() ? Walk.getTime() : 0.0f;
}

bool Fighter::isAttackActive() const { return getMove() && AttackClip->isActiveAt(AttackTime); }

bool Fighter::isStrikingWith(BodyPart Part) const {
    return (isAttackActive() || isJammed()) && !AttackLanded && AttackClip->isStriker(Part);
}

bool Fighter::isAgainstWall() const {
    const float Behind = Body.isFacingRight() ? -1.0f : 1.0f;
    return Body.getPartPosition(BodyPart::Pelvis).X * Behind >= Rules->PelvisLimitX - WallContactM;
}

void Fighter::fillView(FighterView& View) const {
    View.Position = Body.getFloorPoint();
    View.FacingRight = Body.isFacingRight();
    View.Hp = Hp;
    View.MaxHp = Profile.MaxHp;
    View.Stamina = Stamina;
    View.MaxStamina = Profile.MaxStamina;
    View.State = State;
    View.MoveId = getMoveId();
    View.Phase = AttackPhase::None;
    if (getMove()) {
        View.Phase = AttackTime < AttackClip->ActiveBeginSec ? AttackPhase::Startup
                     : AttackClip->isActiveAt(AttackTime)     ? AttackPhase::Active
                                                              : AttackPhase::Recovery;
    }
    View.Block = Guard;
    View.Reaction = State == FighterState::Reacting ? Reaction : ReactionLevel::None;
    View.AgainstWall = isAgainstWall();
    Body.getPartTransforms(View.Parts);
}

void Fighter::drawDebug(std::string_view Name) const {
    if constexpr (FIGHTER_DEBUG) {
        if (State == FighterState::Blocking) {
            // A bar in front of the body over the height the block covers.
            const Vec2 Pelvis = Body.getPartPosition(BodyPart::Pelvis);
            const Vec2 Head = Body.getPartPosition(BodyPart::Head);
            const Vec2 Torso = Body.getPartPosition(BodyPart::Torso);
            const float Neck = (Head.Y + Torso.Y) * 0.5f;
            const float Bottom = Guard == BlockZone::High ? Neck : Guard == BlockZone::Mid ? Pelvis.Y : 0.0f;
            const float Top = Guard == BlockZone::High ? Head.Y + (Head.Y - Neck) : Guard == BlockZone::Mid ? Neck
                                                                                                           : Pelvis.Y;
            const float X = Pelvis.X + (Body.isFacingRight() ? BlockBarOffsetM : -BlockBarOffsetM);
            const std::array Bar = {Vec2{X - BlockBarHalfWidthM, Bottom}, Vec2{X + BlockBarHalfWidthM, Bottom},
                                    Vec2{X + BlockBarHalfWidthM, Top}, Vec2{X - BlockBarHalfWidthM, Top}};
            debug::drawPoly(debug::Cat::Block, Bar);
        }

        debug::setPanel(std::format("{} hp", Name), std::format("{:.1f} / {:.0f}", Hp, Profile.MaxHp));
        debug::setPanel(std::format("{} stamina", Name),
                        std::format("{:.0f} / {:.0f} (+{:.0f}/s){}", Stamina, Profile.MaxStamina,
                                    Profile.StaminaRegen, Exhausted ? "  EXHAUSTED" : ""));
        const ReactionTable& Table = Rules->Reactions;
        debug::setPanel(std::format("{} poise", Name),
                        std::format("buildup {:.2f}, poise {:.2f}: thresholds x{:.2f}", Buildup, Profile.Poise,
                                    getThresholdScale(Table, Profile.Poise, Buildup)));
        if (LastHit) {
            const HitOutcome& Outcome = LastHit->Outcome;
            debug::setPanel(std::format("{} last hit", Name),
                            std::format("{} -> {}: {:.2f} m/s -> {}, {:.1f} dmg{}", LastHit->MoveId,
                                        getBodyPartName(LastHit->Part), Outcome.Strength,
                                        getReactionLevelName(Outcome.Reaction), Outcome.Damage,
                                        Outcome.Blocked ? " (blocked)" : ""));
        } else {
            debug::setPanel(std::format("{} last hit", Name), "-");
        }
        std::string ContactText = "-";
        if (getMove() && Contact == ContactStage::Holding) {
            ContactText = std::format("{} holds the contact, {:.2f} s left", Move->Id, ContactHoldLeftSec);
        } else if (getMove() && Contact == ContactStage::Recovering) {
            ContactText = std::format("{} recovers from the contact", Move->Id);
        }
        if (isJammed()) ContactText += ", jammed in the startup";
        if (Body.isStoppedAtContact()) ContactText += " (stopped this step)";
        debug::setPanel(std::format("{} contact", Name), ContactText);
        debug::setPanel(std::format("{} legs", Name), describeLegs());
        debug::setPanel(std::format("{} upper", Name), describeUpper());
        if (Step.isActive()) Step.drawDebug(LegTarget, Body);
        debug::setPanel(std::format("{} blend", Name), describeBlends());
        // "P1 facing" (and a pending turn) is the rig's panel line.
    }
}

bool Fighter::isFree() const {
    return State == FighterState::Idle || State == FighterState::Walking || State == FighterState::Crouching ||
           State == FighterState::Blocking;
}

void Fighter::setState(FighterState Next) {
    if (Next == State) return;
    if (State == FighterState::Attacking) {
        Move = nullptr;
        AttackFromCrouch = false;
    }
    if (State == FighterState::Reacting) Reaction = ReactionLevel::None;
    State = Next;
    StateSec = 0.0f;
}

void Fighter::updateMeters(float Dt) {
    const ReactionTable& Table = Rules->Reactions;
    Buildup = std::max(0.0f, Buildup - Table.BuildupDecayPerSec * Dt);
    // Attacking and blocking spend stamina; it comes back only in between.
    const bool Spending = State == FighterState::Attacking || State == FighterState::Blocking;
    if (!Spending && State != FighterState::KnockedOut) {
        Stamina = std::min(Profile.MaxStamina, Stamina + Profile.StaminaRegen * Dt);
    }
    if (Exhausted && Stamina >= Rules->Tuning.ExhaustedRecoverFraction * Profile.MaxStamina) Exhausted = false;
}

void Fighter::syncPosture() {
    if (State == FighterState::KnockedOut) return;
    switch (Body.getPosture()) {
        case rig::Posture::KnockedDown: setState(FighterState::KnockedDown); break;
        case rig::Posture::GettingUp: setState(FighterState::GettingUp); break;
        case rig::Posture::Standing:
            if (State == FighterState::KnockedDown || State == FighterState::GettingUp) setState(FighterState::Idle);
            break;
    }
}

const MoveDef* Fighter::advanceAttack(const PlayerCommands& Cmd, const Surroundings& Around, float Dt) {
    AttackTimeBefore = AttackTime;
    if (Contact == ContactStage::Holding) {
        // A posed strike stopped at the opponent holds the contact pose, then
        // recovers: the clip jumps to its recovery, and the pose blends there
        // from the contact pose instead of snapping to the recovery keys.
        ContactHoldLeftSec -= Dt;
        if (ContactHoldLeftSec <= 0.0f) {
            Contact = ContactStage::Recovering;
            AttackTime = std::max(AttackTime, AttackClip->ActiveEndSec);
            AttackTimeBefore = AttackTime;
            Fade.begin(Shown, Rules->Tuning.ContactRecoveryBlendSec);
        }
    } else {
        AttackTime = anim::advanceClipTime(*AttackClip, AttackTime, Dt, AttackRate);
    }
    // A press (not a held button) of a chain button asks for the next strike;
    // it is kept until the cancel window.
    for (const MoveButton Button : AttackButtons) {
        if (isPressed(Cmd, Button) && !isPressed(PreviousCmd, Button) && Move->canChainTo(Button)) {
            ChainRequest = Button;
        }
    }
    if (AttackTime >= AttackClip->ActiveEndSec) {
        RecoverySec += Dt;
        const CombatTuning& Tuning = Rules->Tuning;
        const bool CanChain = AttackHitClean && ChainRequest && ChainLength < Tuning.MaxChainLength &&
                              RecoverySec <= Tuning.ChainWindowSec;
        if (CanChain) {
            if (const MoveDef* Next = findMove(Rules->Moves, *ChainRequest, getWeaponClass(Weapon))) {
                startMove(*Next, Around, ChainLength + 1);
                return Next;
            }
        }
    }
    if (AttackClip->isFinishedAt(AttackTime)) setState(FighterState::Idle);
    return nullptr;
}

const MoveDef* Fighter::chooseFreeState(const PlayerCommands& Cmd, const Surroundings& Around, float Dt) {
    if (const std::optional<BlockZone> Zone = getBlockZone(Cmd, Body.isFacingRight())) {
        if (State != FighterState::Blocking || *Zone != Guard) StateSec = 0.0f;
        Guard = *Zone;
        PendingAttack.reset();
        setState(FighterState::Blocking);
        return nullptr;
    }
    // A strike pressed while crouched starts once the fighter stood up (a
    // block drops it, above). Down still held crouches again after it.
    if (PendingAttack) {
        StandUpLeftSec -= Dt;
        if (StandUpLeftSec > 0.0f) return nullptr;
        const MoveButton Button = *std::exchange(PendingAttack, std::nullopt);
        if (const MoveDef* Next = findMove(Rules->Moves, Button, getWeaponClass(Weapon))) {
            startMove(*Next, Around, 1);
            return Next;
        }
    }
    // Holding an attack button repeats the attack.
    for (const MoveButton Button : AttackButtons) {
        if (!isPressed(Cmd, Button)) continue;
        const MoveDef* Next = findMove(Rules->Moves, Button, getWeaponClass(Weapon));
        if (!Next) continue;
        // Crouched, only the low kick starts at once; the rest stand up first.
        if (State == FighterState::Crouching && Button != MoveButton::LowKick &&
            Rules->Tuning.CrouchStandUpSec > 0.0f) {
            PendingAttack = Button;
            StandUpLeftSec = Rules->Tuning.CrouchStandUpSec;
            setState(FighterState::Idle);
            return nullptr;
        }
        startMove(*Next, Around, 1);
        return Next;
    }
    if (isCrouching(Cmd)) {
        setState(FighterState::Crouching);
        return nullptr;
    }
    // Walking or not is decided with the walk cycle (planWalking).
    if (State != FighterState::Walking) setState(FighterState::Idle);
    return nullptr;
}

void Fighter::startMove(const MoveDef& Next, const Surroundings& Around, int ChainPosition) {
    const float Distance = std::abs(Around.OpponentX - Body.getPartPosition(BodyPart::Pelvis).X);
    // A strike with the legs plays with the foot in front the legs have.
    const anim::Clip& Authored = Rules->Clips.get(Next.getClip(Distance));
    if (anim::usesLegs(Authored)) LegsMirrored = shouldMirrorLegs();
    const anim::Clip& Clip = getPlayed(Authored);
    // A move always starts; without enough stamina it empties it and the
    // fighter is exhausted, so the move itself is already slow.
    spendStamina(Next.Stamina);

    float Rate = Profile.AttackSpeedScale;
    if (!Next.Weapon.empty() && Weapon) Rate *= Weapon->SpeedScale;
    if (Exhausted) Rate *= Rules->Tuning.ExhaustedSpeedScale;

    const bool FromCrouch = State == FighterState::Crouching;
    setState(FighterState::Attacking);
    StateSec = 0.0f;
    AttackFromCrouch = FromCrouch;
    Move = &Next;
    AttackClip = &Clip;
    AttackTime = 0.0f;
    AttackTimeBefore = 0.0f;
    Contact = ContactStage::None;
    ContactHoldLeftSec = 0.0f;
    Jammed = false;
    AttackRate = Rate;
    TopRestarted = true;
    AttackLanded = false;
    AttackHitClean = false;
    RecoverySec = 0.0f;
    ChainLength = ChainPosition;
    ChainRequest.reset();
}

float Fighter::planWalking(const PlayerCommands& Cmd) {
    const CombatTuning& Tuning = Rules->Tuning;
    Stride = {};
    if (Body.getPosture() != rig::Posture::Standing) {
        // Down or getting up, the legs come back into the stance.
        RestFront = BodyPart::FootL;
        Walk.settle(RestFront);
        CrouchWalk.settle();
        return 0.0f;
    }
    const bool Crouched = State == FighterState::Crouching;
    if (!Crouched) CrouchWalk.settle();
    Stride.Crouched = Crouched;
    const bool AttackAllowsMove = State == FighterState::Attacking && AttackClip->AllowMove && !AttackFromCrouch;
    const bool BlockAllowsMove = State == FighterState::Blocking && Rules->Clips.getBlock(Guard).AllowMove;
    const bool CanMove = !PendingAttack && (State == FighterState::Idle || State == FighterState::Walking ||
                                            Crouched || BlockAllowsMove || AttackAllowsMove);
    float MoveX = CanMove ? std::clamp(Cmd.MoveX, -1.0f, 1.0f) : 0.0f;
    const bool Forward = (MoveX > 0.0f) == Body.isFacingRight();
    // The wall behind: no step back, and no marching on the spot. Blocking:
    // no step forward.
    if (!Forward && isAgainstWall()) MoveX = 0.0f;
    if (Forward && State == FighterState::Blocking) MoveX = 0.0f;
    const bool WantsToMove = std::abs(MoveX) > MoveDeadZone;

    const rig::ControlParams& Control = Body.getControl();
    float Scale = Forward ? 1.0f : Control.BackwardSpeedScale;
    if (State == FighterState::Blocking) Scale = Tuning.BlockBackSpeedScale;
    if (Crouched) Scale *= Tuning.CrouchWalkSpeedScale;
    if (Exhausted) Scale *= Tuning.ExhaustedSpeedScale;

    Stride.WantsToMove = WantsToMove;
    Stride.Sign = MoveX > 0.0f ? 1.0f : -1.0f;
    return WantsToMove ? MoveX * Body.getWalkSpeed() * Scale : 0.0f;
}

void Fighter::advanceLegs(float Dt) {
    if (Body.getPosture() != rig::Posture::Standing) return;
    const CombatTuning& Tuning = Rules->Tuning;
    const bool Crouched = Stride.Crouched;
    LegCycle& Cycle = Crouched ? CrouchWalk : Walk;
    // The cycle follows the distance the pelvis covers, so the feet keep up
    // with it. The crouch walk plays once per period at the full crouch
    // walking speed.
    const float CycleSpeed = Body.getWalkSpeed() * (Crouched ? Tuning.CrouchWalkSpeedScale : 1.0f);
    const rig::PelvisController& Motion = Body.getController();
    const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
    const float MinTravel = Tuning.StepMinSpeed * Dt;
    if (!Stride.WantsToMove) {
        // Not walking: the walk stops on both feet. A fighter pushed by the
        // opponent's body (the carry) or knocked back by a hit does not step
        // with it: its feet are at the opponent's, and a foot lifted there
        // would come down on them; the planted feet go along with the push
        // (Rig::pushBody) or hold their place and step back under the body
        // afterwards (the rig's footRestepDistance).
        stopLegs(Cycle, Crouched, Dt);
        return;
    }
    // The planned travel of this step (the walk, and a knockback or a push
    // going on); the spacing may take some of it back, the cycle then keeps
    // only that share (applyControl()). Held in place by the opponent (it
    // planned to go, the pelvis did not), the legs hold their pose instead
    // of marching on the spot. (Playing the stop here would move the feet
    // without travel, into the opponent.)
    const float Travel = Motion.getPlannedTravel();
    const float LastTravel = Motion.getVelocity() * Dt;
    Stride.Held = LastPlannedTravel * Stride.Sign > MinTravel && LastTravel * Stride.Sign < MinTravel;
    // Slowed by the opponent in the last step: it is in the way.
    WalkHeld = Motion.getSpacingMotion().Slowed * Stride.Sign < 0.0f;
    // Pushed back faster than it walks, the walker steps backwards.
    Stride.Pushed = Travel * Stride.Sign < 0.0f;
    FeetSettling = false;
    Cycle.walk(Dt, std::abs(Travel) / (CycleSpeed * Dt), Travel * Facing);
    Stride.FollowsTravel = true;
    Stride.Travel = Travel;
}

void Fighter::stopLegs(LegCycle& Cycle, bool Crouched, float Dt) {
    const CombatTuning& Tuning = Rules->Tuning;
    if (Cycle.getMode() == LegCycle::Mode::Walking) FeetSettling = true;
    // A walk the opponent holds back stops where it is (LegCycle::hold):
    // playing the step on without travel would set the swing foot down on
    // the opponent's, and the spacing would shove them apart.
    if (WalkHeld) {
        Cycle.hold();
    } else {
        Cycle.stop(Dt, Tuning.WalkStopRate);
    }
    if (!Cycle.isPlaying() && !Cycle.isHeld()) WalkHeld = false;
    if (FeetSettling) {
        // Played on quickly, the cycle moves the planted foot relative to the
        // pelvis: the rig holds it where it stands (the leg bends to it), and
        // from then on the feet stay where they stand, also when the pelvis
        // glides on a little, instead of stepping under the body once more
        // (rig::Rig::keepFeetPlanted). The legs rest in the cycle's pose.
        Body.keepFeetPlanted();
        const bool Settled = !Cycle.isStopping() && !LegFade.isActive() && !(Crouched && Fade.isActive());
        if (Settled && Body.getController().getWalkVelocity() == 0.0f) FeetSettling = false;
    }
}

Fighter::TargetPoses Fighter::buildTargetPose(const anim::Clip* Top, bool TopChanged, float Dt) {
    const ClipLibrary& Clips = Rules->Clips;
    const anim::Pose Stance = anim::sampleClip(Clips.get(clips::Stance), 0.0f);
    const bool LegAction = Top && anim::usesLegs(*Top);
    updateRestLegs(LegAction, Dt);

    // Below a leg action: the stance (the crouch for an attack from it),
    // with the legs swapped when the action plays mirrored.
    anim::Pose Base = Stance;
    if (LegAction && LegsMirrored) Base = anim::joinLayers(Stance, anim::mirrorLegs(Stance));
    if (State == FighterState::Attacking && AttackFromCrouch) {
        anim::layerPose(Base, anim::sampleClip(getPlayed(Clips.get(clips::Crouch)), 0.0f));
    }
    // Crouched, the crouch walk sets the legs once it has played, and holds
    // where it stopped.
    const bool WithCrouchWalk = State == FighterState::Crouching && CrouchWalk.isEngaged();
    const anim::Clip& CrouchWalkClip = getPlayed(Clips.get(clips::CrouchWalk));
    // The two layers, over the legs of the step and over those without its
    // travel: the upper body plays the clip on top over the base; the legs
    // play the leg action, or rest (or walk) as they are.
    const auto compose = [&](const anim::Pose& RestLegs, float CrouchWalkTime) {
        anim::Pose Upper = Base;
        anim::Pose Legs = RestLegs;
        if (Top) anim::layerPose(Upper, anim::sampleClip(*Top, getTopClipTime()));
        if (LegAction) {
            Legs = Upper;
            if (WithCrouchWalk) anim::layerPose(Legs, anim::sampleClip(CrouchWalkClip, CrouchWalkTime));
        }
        return anim::joinLayers(Upper, Legs);
    };
    TargetPoses Result{.Moving = compose(ShownLegs, CrouchWalk.getTime()),
                       .Still = compose(ShownLegsStill, CrouchWalk.getStepFromTime())};
    if (State == FighterState::Crouching && WithCrouchWalk != ShowsCrouchWalk) {
        beginBlend(Fade, Shown, TopBlend, WithCrouchWalk ? PoseKind::Crouch : PoseKind::CrouchWalk,
                   WithCrouchWalk ? PoseKind::CrouchWalk : PoseKind::Crouch);
    }
    ShowsCrouchWalk = WithCrouchWalk;

    // A leg action takes the legs by real steps (LegStep); any other clip
    // leaves them alone.
    if (!LegAction) {
        Step.cancel();
        LegsStepped = false;
        return Result;
    }
    if (TopChanged) startLegStep(*Top, Result.Moving);
    const bool Stepping = Step.isActive();
    Step.advance(Dt, Body.measureLegsNow());
    LegTarget = Body.measureLegs(Result.Moving.Angles);
    Step.apply(Result.Moving.Angles, Body);
    Step.apply(Result.Still.Angles, Body);
    // The rig would take a planted foot held off its old pose for one left
    // behind by a push and step it back: the feet stay where the steps put
    // them.
    if (Stepping) Body.keepFeetPlanted();
    return Result;
}

void Fighter::updateRestLegs(bool LegAction, float Dt) {
    const anim::Clip& WalkClip = Rules->Clips.get(clips::Walk);
    const LegSource Source = Walk.isEngaged() ? LegSource::Walk : LegSource::Stance;
    anim::Pose Legs;
    anim::Pose LegsStill;
    if (Source == LegSource::Walk) {
        Legs = anim::sampleClip(WalkClip, Walk.getTime());
        LegsStill = anim::sampleClip(WalkClip, Walk.getStepFromTime());
    } else {
        Legs = getStanceLegs(RestFront);
        LegsStill = Legs;
    }
    if (LegAction) {
        // The action has the legs; these are the legs it leaves.
        LegFade.cancel();
    } else if (Source != ShownSource) {
        const auto getKind = [](LegSource Legs) { return Legs == LegSource::Walk ? PoseKind::Walk : PoseKind::Stance; };
        beginBlend(LegFade, ShownLegs, LegBlend, getKind(ShownSource), getKind(Source));
    }
    ShownSource = Source;
    ShownLegs = LegFade.step(Legs, Dt);
    ShownLegsStill = LegFade.peek(LegsStill);
}

void Fighter::startLegStep(const anim::Clip& Top, const anim::Pose& Target) {
    const LegStepTuning& Tuning = Rules->Tuning.LegStep;
    // A strike steps within its startup, so it comes on the same tick as
    // from the stance; an action without one takes the tuning's time.
    const bool Strike = State == FighterState::Attacking && Top.ActiveBeginSec > 0.0f;
    const float Sec = Strike ? anim::getStartupAtRate(Top, AttackRate) * Tuning.StartupShare : Tuning.Sec;
    const anim::Pose& Mask = Top.Keys.front().Target;
    const bool ClipLeft = Mask.hasJoint(BodyPart::ThighL) || Mask.hasJoint(BodyPart::ShinL);
    const bool ClipRight = Mask.hasJoint(BodyPart::ThighR) || Mask.hasJoint(BodyPart::ShinR);
    Step = LegStep::plan(Body.measureLegsNow(), Body.measureLegs(Target.Angles), ClipLeft, ClipRight, Sec, Tuning);
    LegsStepped = Step.isActive();
    if constexpr (FIGHTER_DEBUG) {
        if (Step.isActive()) {
            debug::logEvent(std::format("P{} steps into {} in {:.2f} s: {}", Body.getFighterIndex() + 1, Top.Name, Sec,
                                        Step.describe()));
        }
    }
}

const anim::Clip& Fighter::getPlayed(const anim::Clip& Authored) const {
    return LegsMirrored && anim::usesLegs(Authored) ? Rules->Clips.getMirrored(Authored) : Authored;
}

bool Fighter::shouldMirrorLegs() const {
    return Rules->Tuning.LegStep.Stance == StanceAfterStop::Mirror &&
           Body.measureLegsNow().getFrontFoot() == BodyPart::FootR;
}

anim::Pose Fighter::getStanceLegs(BodyPart FrontFoot) const {
    const anim::Pose Legs = anim::selectJoints(anim::sampleClip(Rules->Clips.get(clips::Stance), 0.0f),
                                               anim::getLegJoints());
    return FrontFoot == BodyPart::FootR ? anim::mirrorLegs(Legs) : Legs;
}

std::string Fighter::describeLegs() const {
    if (PendingAttack) {
        return std::format("standing up {:.2f} s -> {}", StandUpLeftSec, getMoveButtonName(*PendingAttack));
    }
    const std::string Steps = Step.isActive() ? ", " + Step.describe() : "";
    if (State == FighterState::Crouching) {
        const std::string_view Side = LegsMirrored ? " (mirrored)" : "";
        if (CrouchWalk.isStopping()) {
            return std::format("crouch walk{} {:.2f} stopping{}", Side, CrouchWalk.getTime(), Steps);
        }
        if (CrouchWalk.isPlaying()) return std::format("crouch walk{} {:.2f}{}", Side, CrouchWalk.getTime(), Steps);
        if (CrouchWalk.isEngaged()) return std::format("crouch{}, held at {:.2f}{}", Side, CrouchWalk.getTime(), Steps);
        return std::format("crouch{}{}", Side, Steps);
    }
    if (const anim::Clip* Top = getTopClip(); Top && anim::usesLegs(*Top)) {
        return std::format("action {}{}", Top->Name, Step.isActive() ? ": " + Step.describe() : "");
    }
    switch (Walk.getMode()) {
        case LegCycle::Mode::Walking:
            return std::format("walking {:.2f}{}", Walk.getTime(),
                               Stride.Held     ? ", held by the opponent"
                               : Stride.Pushed ? ", pushed back"
                                               : "");
        case LegCycle::Mode::Held:
            return std::format("held in mid-step at {:.2f} (opponent in the way)", Walk.getTime());
        case LegCycle::Mode::Stopping:
            return std::format("stopping {:.2f} -> phase {:.2f} (front {})", Walk.getTime(), Walk.getStopTarget(),
                               getBodyPartName(Walk.getFrontFoot()));
        case LegCycle::Mode::Still: break;
    }
    if (Walk.isEngaged()) {
        return std::format("resting at phase {:.2f} (front {})", Walk.getTime(), getBodyPartName(Walk.getFrontFoot()));
    }
    return std::format("resting in the stance (front {})", getBodyPartName(RestFront));
}

std::string Fighter::describeUpper() const {
    const anim::Clip* Top = getTopClip();
    if (!Top || !hasUpperJoints(*Top)) return "stance";
    const float Rate = State == FighterState::Attacking ? AttackRate : 1.0f;
    return anim::describePlayback(*Top, getTopClipTime(), Rate, Fade);
}

void Fighter::beginTopFade(const anim::Clip* Top) {
    const BlendTable& Blends = Rules->Tuning.Blends;
    const PoseKind LegKind = Walk.isPlaying() ? PoseKind::Walk : PoseKind::Stance;
    const PoseKind From = ShownTop ? getClipKind(*ShownTop) : LegKind;
    const PoseKind To = Top ? getClipKind(*Top) : LegKind;
    float Sec = Blends.getSec(From, To);
    // The clip's own time wins: into it, or out of it.
    if (Top && Top->BlendInSec) {
        Sec = *Top->BlendInSec;
    } else if (!Top && ShownTop && ShownTop->BlendOutSec) {
        Sec = *ShownTop->BlendOutSec;
    }
    // A strike shows its own pose by the share of its startup: the blend
    // does not delay it.
    if (Top && State == FighterState::Attacking && To == PoseKind::Strike) {
        Sec = std::min(Sec, anim::getStartupAtRate(*Top, AttackRate) * Blends.StrikeStartupShare);
    }
    Fade.begin(Shown, Sec);
    TopBlend = {.From = From, .To = To, .Sec = Sec};
}

void Fighter::beginBlend(anim::PoseTransition& Transition, const anim::Pose& From, BlendInfo& Info,
                         PoseKind FromKind, PoseKind ToKind) {
    const float Sec = Rules->Tuning.Blends.getSec(FromKind, ToKind);
    Transition.begin(From, Sec);
    Info = {.From = FromKind, .To = ToKind, .Sec = Sec};
}

PoseKind Fighter::getClipKind(const anim::Clip& Played) const {
    const ClipLibrary& Clips = Rules->Clips;
    const anim::Clip& Source = Clips.getAuthored(Played);
    if (&Source == &Clips.get(clips::Crouch)) return PoseKind::Crouch;
    if (&Source == &Clips.get(clips::CrouchWalk)) return PoseKind::CrouchWalk;
    for (const BlockZone Zone : {BlockZone::High, BlockZone::Mid, BlockZone::Low}) {
        if (&Source == &Clips.getBlock(Zone)) return PoseKind::Block;
    }
    for (const ReactionLevel Level : {ReactionLevel::Flinch, ReactionLevel::Stagger, ReactionLevel::Knockback}) {
        if (&Source == Clips.findReaction(Level)) return PoseKind::Reaction;
    }
    return PoseKind::Strike;
}

std::string Fighter::describeBlends() const {
    const auto describe = [](const anim::PoseTransition& Transition, const BlendInfo& Info) {
        if (!Transition.isActive()) return std::string("-");
        return std::format("{} -> {} {:.2f} s, {:.2f}", getPoseKindName(Info.From), getPoseKindName(Info.To),
                           Info.Sec, Transition.getWeight());
    };
    return std::format("pose {}; legs {}", describe(Fade, TopBlend), describe(LegFade, LegBlend));
}

const anim::Clip* Fighter::getTopClip() const {
    switch (State) {
        case FighterState::Attacking: return AttackClip;
        case FighterState::Crouching: return &getPlayed(Rules->Clips.get(clips::Crouch));
        case FighterState::Blocking: return &getPlayed(Rules->Clips.getBlock(Guard));
        case FighterState::Reacting: return Rules->Clips.findReaction(Reaction);
        default: return nullptr;
    }
}

float Fighter::getTopClipTime() const { return State == FighterState::Attacking ? AttackTime : StateSec; }

void Fighter::spendStamina(float Amount) {
    Stamina = std::max(0.0f, Stamina - Amount);
    if (Stamina > 0.0f || Exhausted) return;
    Exhausted = true;
    ExhaustedNotice = true;
}

void Fighter::react(ReactionLevel Level, float Impulse, float Direction, Vec2 Point) {
    const Vec2 Push{Direction, 0.0f};
    if (Hp <= 0.0f) {
        // Knocked out: it falls and stays down.
        Body.applyHit(Impulse, Push, Point, true);
        Body.setStayDown(true);
        setState(FighterState::KnockedOut);
        return;
    }
    const bool KnockDown = Level == ReactionLevel::Knockdown;
    // The rig sways the body (physics), pushes the pelvis back by
    // impulse / mass and, for a knockdown, lets it fall the way it was
    // pushed, spun by where the hit landed.
    Body.applyHit(Impulse, Push, Point, KnockDown);
    if (KnockDown) {
        setState(FighterState::KnockedDown);
        return;
    }
    // Getting up is the rig's; a weaker hit does not interrupt it. Touch and
    // None interrupt nothing.
    if (State == FighterState::GettingUp || Level < ReactionLevel::Flinch) return;

    // During a reaction a new hit only raises the level, never lowers it.
    // The stun lasts the longer of what is left and the new hit's own stun:
    // weak hits do not keep a stronger reaction going (no stun lock, O.7).
    const bool Raised = State != FighterState::Reacting || Level > Reaction;
    const ReactionLevel Effective = State == FighterState::Reacting ? std::max(Reaction, Level) : Level;
    const float StunSec = Rules->Reactions.getLevel(Level).StunSec;
    StunLeftSec = State == FighterState::Reacting ? std::max(StunLeftSec, StunSec) : StunSec;
    setState(FighterState::Reacting);
    Reaction = Effective;
    if (Raised) {
        StateSec = 0.0f;   // the clip of the new level starts
        TopRestarted = true;
    }
}

namespace {

std::string_view getWeaponClass(const std::optional<stats::WeaponProps>& Weapon) {
    return Weapon ? std::string_view(Weapon->Class) : std::string_view();
}

/// Does the clip pose a joint of the upper body (not only the legs)?
bool hasUpperJoints(const anim::Clip& Source) {
    return (Source.Keys.front().Target.Mask & ~anim::getLegJoints()).any();
}

} // namespace

} // namespace fighter::combat
