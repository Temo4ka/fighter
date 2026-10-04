#include "combat/fighter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

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
}

const MoveDef* Fighter::control(const PlayerCommands& Cmd, const Surroundings& Around, float Dt) {
    updateMeters(Dt);
    syncPosture();
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
        Started = chooseFreeState(Cmd, Around);
        // The rig mirrors the body in the next applyControl().
        if (DesiredFacingRight != Body.isFacingRight()) Body.setFacing(DesiredFacingRight);
    }

    const float Velocity = planWalking(Cmd, Dt);
    if (State == FighterState::Idle && Walking) setState(FighterState::Walking);
    if (State == FighterState::Walking && !Walking) setState(FighterState::Idle);

    anim::Pose Target = anim::sampleClip(Rules->Clips.get(clips::Stance), 0.0f);
    if (Walking) anim::layerPose(Target, anim::sampleClip(Rules->Clips.get(clips::Walk), WalkTime));
    const anim::Clip* Top = getTopClip();
    if (Top) anim::layerPose(Target, anim::sampleClip(*Top, getTopClipTime()));
    // A clip that starts fades in, one that ends fades out (its own times).
    if (Top != ShownTop || TopRestarted) {
        if (Top) {
            Fade.begin(Shown, Top->BlendInSec);
        } else {
            Fade.begin(Shown, ShownTop->BlendOutSec);
        }
    }
    ShownTop = Top;
    TopRestarted = false;
    Shown = Fade.step(Target, Dt);

    Body.setTargetAngles(Shown.Angles);
    Body.setMoveVelocity(Velocity);
    Body.setBaseStiffness(Top ? Top->Stiffness : 1.0f);
    Body.planMotion(Dt);
    PreviousCmd = Cmd;
    return Started;
}

void Fighter::applyControl(float Dt) { Body.applyControl(Dt); }

bool Fighter::isHittable() const { return State != FighterState::KnockedDown && State != FighterState::KnockedOut; }

HitOutcome Fighter::takeHit(const physics::HitEvent& Hit, const MoveDef& Attack, float PowerScale,
                            float Direction) {
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
        .MinReaction = Attack.MinReaction,
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
    // In the startup only if the tuning says so: such a contact is not a
    // hit, so it jams the attack.
    if (!getMove()) return;
    const CombatTuning& Tuning = Rules->Tuning;
    const bool Startup = AttackTime < AttackClip->ActiveBeginSec;
    if (Contact == ContactStage::None && Startup && !Tuning.ContactStopInStartup) return;
    const std::optional<float> Kept =
        Body.stopAtContact(AttackClip->Strikers, Tuning.ContactStopDepth, Contact != ContactStage::None);
    if (!Kept || Contact != ContactStage::None || AttackTimeBefore >= AttackClip->ActiveEndSec) return;

    // The first stop: the clip goes back to the time of the contact (clip
    // time advances evenly within a step) and holds there. A contact of the
    // striking phase stays in it, so one that was too slow to be a hit can
    // still land.
    const float Stopped = AttackTimeBefore + (AttackTime - AttackTimeBefore) * *Kept;
    AttackTime = Startup ? Stopped : std::clamp(Stopped, AttackClip->ActiveBeginSec, AttackClip->ActiveEndSec);
    Contact = ContactStage::Holding;
    ContactHoldLeftSec = Tuning.ContactHoldSec;
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} stopped at the opponent's posed parts (clip {:.2f} s)",
                                    Body.getFighterIndex() + 1, Move->Id, AttackTime));
    }
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
    const anim::Clip& Playing = Top ? *Top : Rules->Clips.get(Walking ? clips::Walk : clips::Stance);
    const float Rate = State == FighterState::Attacking ? AttackRate : 1.0f;
    return anim::describePlayback(Playing, getClipTime(), Rate, Fade);
}

float Fighter::getClipTime() const {
    if (getTopClip()) return getTopClipTime();
    return Walking ? WalkTime : 0.0f;
}

bool Fighter::isAttackActive() const { return getMove() && AttackClip->isActiveAt(AttackTime); }

bool Fighter::isStrikingWith(BodyPart Part) const {
    return isAttackActive() && !AttackLanded && AttackClip->isStriker(Part);
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
        if (Body.isStoppedAtContact()) ContactText += " (stopped this step)";
        debug::setPanel(std::format("{} contact", Name), ContactText);
        // "P1 facing" (and a pending turn) is the rig's panel line.
    }
}

bool Fighter::isFree() const {
    return State == FighterState::Idle || State == FighterState::Walking || State == FighterState::Crouching ||
           State == FighterState::Blocking;
}

void Fighter::setState(FighterState Next) {
    if (Next == State) return;
    if (State == FighterState::Attacking) Move = nullptr;
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

const MoveDef* Fighter::chooseFreeState(const PlayerCommands& Cmd, const Surroundings& Around) {
    if (const std::optional<BlockZone> Zone = getBlockZone(Cmd, Body.isFacingRight())) {
        if (State != FighterState::Blocking || *Zone != Guard) StateSec = 0.0f;
        Guard = *Zone;
        setState(FighterState::Blocking);
        return nullptr;
    }
    // Holding an attack button repeats the attack.
    for (const MoveButton Button : AttackButtons) {
        if (!isPressed(Cmd, Button)) continue;
        const MoveDef* Next = findMove(Rules->Moves, Button, getWeaponClass(Weapon));
        if (!Next) continue;
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
    const anim::Clip& Clip = Rules->Clips.get(Next.getClip(Distance));
    // A move always starts; without enough stamina it empties it and the
    // fighter is exhausted, so the move itself is already slow.
    spendStamina(Next.Stamina);

    float Rate = Profile.AttackSpeedScale;
    if (!Next.Weapon.empty() && Weapon) Rate *= Weapon->SpeedScale;
    if (Exhausted) Rate *= Rules->Tuning.ExhaustedSpeedScale;
    // However fast the fighter, the active phase starts no sooner than the
    // move allows (O.7): the whole clip plays slower for that.
    Rate = anim::limitRateByStartup(Clip, Rate, Next.MinStartupSec);

    setState(FighterState::Attacking);
    StateSec = 0.0f;
    Move = &Next;
    AttackClip = &Clip;
    AttackTime = 0.0f;
    AttackTimeBefore = 0.0f;
    Contact = ContactStage::None;
    ContactHoldLeftSec = 0.0f;
    AttackRate = Rate;
    TopRestarted = true;
    AttackLanded = false;
    AttackHitClean = false;
    RecoverySec = 0.0f;
    ChainLength = ChainPosition;
    ChainRequest.reset();
}

float Fighter::planWalking(const PlayerCommands& Cmd, float Dt) {
    const bool Standing = Body.getPosture() == rig::Posture::Standing;
    const bool AttackAllowsMove = State == FighterState::Attacking && AttackClip->AllowMove;
    const bool CanMove = Standing && (State == FighterState::Idle || State == FighterState::Walking ||
                                      State == FighterState::Blocking || AttackAllowsMove);
    float MoveX = CanMove ? std::clamp(Cmd.MoveX, -1.0f, 1.0f) : 0.0f;
    const bool Forward = (MoveX > 0.0f) == Body.isFacingRight();
    // The wall behind: no step back, and no marching on the spot.
    if (!Forward && isAgainstWall()) MoveX = 0.0f;
    const bool WantsToMove = std::abs(MoveX) > MoveDeadZone;

    const rig::ControlParams& Control = Body.getControl();
    const CombatTuning& Tuning = Rules->Tuning;
    float Scale = Forward ? 1.0f : Control.BackwardSpeedScale;
    if (State == FighterState::Blocking) Scale *= Tuning.BlockWalkSpeedScale;
    if (Exhausted) Scale *= Tuning.ExhaustedSpeedScale;
    const float Period = Rules->Clips.get(clips::Walk).DurationSec;

    float Velocity = 0.0f;
    if (WantsToMove) {
        Velocity = MoveX * Body.getWalkSpeed() * Scale;
        // The cycle follows the distance actually covered, so the feet keep
        // up with the pelvis and do not march on the spot when the way is
        // blocked.
        const float Rate = std::abs(Body.getController().getVelocity()) / Body.getWalkSpeed();
        WalkDirection = Forward ? 1.0f : -1.0f;
        WalkTime = std::fmod(WalkTime + Dt * Rate * WalkDirection + Period, Period);
        Walking = true;
    } else if (Walking && Standing) {
        // Finish the step: the cycle plays on to phase 0, where the legs are
        // placed as in the stance.
        const float Next = WalkTime + Dt * WalkDirection;
        Walking = Next > 0.0f && Next < Period;
        WalkTime = Walking ? Next : 0.0f;
    } else {
        Walking = false;
        WalkTime = 0.0f;
    }
    return Velocity;
}

const anim::Clip* Fighter::getTopClip() const {
    switch (State) {
        case FighterState::Attacking: return AttackClip;
        case FighterState::Crouching: return &Rules->Clips.get(clips::Crouch);
        case FighterState::Blocking: return &Rules->Clips.getBlock(Guard);
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

} // namespace

} // namespace fighter::combat
