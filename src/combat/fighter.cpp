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

/// Stop targets whose planted feet are this close in how far they are off
/// the cycle are as good: the nearer one wins, m.
constexpr float StopTieM = 0.01f;

/// How close counts as reached for a foot placed by IK, m.
constexpr float ReachToleranceM = 0.01f;
/// A settle is over with the pelvis this close to its place in the stance, m...
constexpr float SettleEndM = 0.003f;
/// ...or once the steps are over and the pelvis has made no headway (slower
/// than SettleStuckSpeed, m/s) for SettleStuckSec, or after SettleMaxSec.
constexpr float SettleStuckSpeed = 0.02f;
constexpr float SettleStuckSec = 0.1f;
constexpr float SettleMaxSec = 1.5f;
/// A swing foot on its way is at least this many times the plant height
/// off the floor.
constexpr float SwingClearance = 2.0f;
/// Knockback slower than this lets a resting foot step again, m/s.
constexpr float CalmKnockback = 0.05f;

/// A step of a clip's pelvis track that made this much less than planned
/// was held back (the opponent, a wall), m.
constexpr float HeldTrackM = 1e-4f;
/// The arrows of the pelvis track: this far above the pelvis, and the
/// "made" one this much below the "track" one, m.
constexpr float PelvisTrackDrawLiftM = 0.12f;
constexpr float PelvisTrackDrawGapM = 0.05f;

/// Width and offset of the Block zone drawn in front of the body, m.
constexpr float BlockBarOffsetM = 0.3f;
constexpr float BlockBarHalfWidthM = 0.04f;

bool hasUpperJoints(const anim::Clip& Source);
/// "High", "Mid", "Low": the zone as data files write it.
std::string_view getBlockZoneName(BlockZone Zone);
/// The names with \p Separator between them.
std::string joinNames(const std::vector<std::string>& Names, std::string_view Separator);
/// The wrist \p Shown sets for the rig (rig::Rig::setWristAngle).
std::optional<float> getWristWish(const anim::Pose& Shown);

} // namespace

Fighter::Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const BattleRules& NewRules,
                 const stats::PhysicalProfile& NewProfile, const stats::Loadout& Gear,
                 const rig::RigSetup& Setup, std::optional<float> StartHp)
    : Body(PhysWorld, Description, Setup), Rules(&NewRules), Profile(NewProfile),
      Hp(std::clamp(StartHp.value_or(NewProfile.MaxHp), 0.0f, NewProfile.MaxHp)), Stamina(NewProfile.MaxStamina),
      DesiredFacingRight(Setup.FacingRight) {
    for (const stats::EquipmentSlot Hand : {stats::EquipmentSlot::MainHand, stats::EquipmentSlot::OffHand}) {
        const stats::EquipmentItem* Item = Gear.findInSlot(Hand);
        if (!Item || Item->Slot != Hand || !Item->Weapon) continue;
        Weapons.push_back({.Part = Hand == stats::EquipmentSlot::MainHand ? Description.Weapon.Part
                                                                          : Description.Weapon.OffPart,
                           .Props = *Item->Weapon,
                           .ItemName = Item->Name});
    }
    // A shield in the off hand guards and gives no strikes of its own; it
    // still makes a pair with the main hand's item.
    Set = &NewRules.Moves.selectSet(Gear.getMoveSet(stats::EquipmentSlot::MainHand),
                                    Gear.getMoveSet(stats::EquipmentSlot::OffHand),
                                    !Gear.findShield(stats::EquipmentSlot::OffHand));
    Block = NewRules.Moves.getBlock(*Set, getDefaultBlock(NewRules.Reactions));
    StanceName = NewRules.Moves.getStance(*Set, clips::Stance);
    Shown = getStancePose();
    Body.setTargetAngles(Shown.Angles);
    Body.setWristAngle(getWristWish(Shown));
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
    OpponentGap = std::abs(Around.OpponentX - Body.getPartPosition(BodyPart::Pelvis).X);
    OpponentX = Around.OpponentX;
    StateSec += Dt;
    // Only presses the fighter may act on count: none are kept through a
    // reaction or a fall to start a move afterwards.
    Presses.update(getNewlyPressed(Cmd, PreviousCmd), Rules->Moves.getInputRules().ComboWindowSec, Dt);
    if (!isFree() && State != FighterState::Attacking) Presses.clear();

    // Where the opponent is. The body turns only when the fighter is free
    // to act: not during an attack, a reaction, on the floor or getting up.
    const float OwnX = Body.getPartPosition(BodyPart::Pelvis).X;
    if (std::abs(Around.OpponentX - OwnX) > FacingDeadZoneM) DesiredFacingRight = Around.OpponentX > OwnX;

    const MoveDef* Started = nullptr;
    Lunge.Planned = 0.0f;
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

    // The pelvis plans its motion first: the legs step with its travel. The
    // pelvis track of the attack's clip is planned motion too: the spacing
    // and the walls stop it like the walk (checkPelvisTrack()).
    const bool FacesRight = Body.isTurnPending() ? !Body.isFacingRight() : Body.isFacingRight();
    if (Lunge.Planned != 0.0f) Lunge.Facing = FacesRight ? 1.0f : -1.0f;
    Body.getController().setClipTravel(Lunge.Planned * Lunge.Facing);
    Body.setMoveVelocity(planWalking(Cmd));
    Body.planMotion(Dt);
    advanceLegs(Dt);
    if (State == FighterState::Idle && Walk.isPlaying()) setState(FighterState::Walking);
    if (State == FighterState::Walking && !Walk.isPlaying()) setState(FighterState::Idle);

    // A crouch or a low block that starts now plays with the foot in front
    // the legs have (an attack chose in startMove()).
    if (State == FighterState::Crouching || State == FighterState::Blocking) {
        const ClipLibrary& Clips = Rules->Clips;
        const anim::Clip& Authored = State == FighterState::Crouching ? Clips.get(clips::Crouch) : getBlockClip();
        const bool Continues = ShownTop && &Clips.getAuthored(*ShownTop) == &Authored;
        if (anim::usesLegs(Authored) && !Continues) LegsMirrored = shouldMirrorLegs();
    }
    const anim::Clip* Top = getTopClip();
    const bool TopChanged = Top != ShownTop || TopRestarted;
    // Legs leaving a leg action fade on the leg layer, over the action's
    // blend-out (not the next clip's blend-in: a flinch fades in in 0.02 s).
    const bool LegAction = Top && anim::usesLegs(*Top);
    if (TopChanged && !LegAction && ShownTop && anim::usesLegs(*ShownTop)) {
        const float Sec =
            ShownTop->BlendOutSec.value_or(Rules->Tuning.Blends.getSec(getClipKind(*ShownTop), PoseKind::Stance));
        LegFade.begin(anim::selectJoints(Shown, anim::getLegJoints()), Sec);
        LegBlend = {.From = getClipKind(*ShownTop), .To = PoseKind::Stance, .Sec = Sec};
        LegFadeByTravel = false;
    }
    const TargetPoses Target = buildTargetPose(Top, TopChanged, Dt);
    // Over the planted rear foot of a lunge the pelvis may go down deeper,
    // until the feet are back in the stance.
    if (Body.getPosture() != rig::Posture::Standing) LungeLegs = false;
    Body.setPelvisDropLimit(LungeLegs ? std::optional(Rules->Tuning.LegStep.LungePelvisDrop) : std::nullopt);
    // A leg action ends the walk where it rested: afterwards the legs rest
    // in the stance it leaves them in.
    if (Top && anim::usesLegs(*Top)) {
        RestFront = LegsMirrored ? BodyPart::FootR : BodyPart::FootL;
        Walk.settle(RestFront);
        clearRest();
    }
    // A clip that starts fades in, one that ends fades out: the clip's own
    // time or the blend table's for the change.
    if (TopChanged) beginTopFade(Top);
    ShownTop = Top;
    TopRestarted = false;
    Shown = Fade.step(Target.Moving, Dt);
    ShownStill = Fade.peek(Target.Still);
    // Legs that step into the action's pose are not faded: the steps are
    // the transition. Nor are the resting or walking legs of the leg layer:
    // their own fades (LegFade) and steps are.
    if (LegsStepped || !LegAction) {
        anim::layerPose(Shown, anim::selectJoints(Target.Moving, anim::getLegJoints()));
        anim::layerPose(ShownStill, anim::selectJoints(Target.Still, anim::getLegJoints()));
    }

    Body.setTargetAngles(Shown.Angles);
    Body.setWristAngle(getWristWish(Shown));
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
        WalkOdometer += Stride.Travel * Share * (Body.isFacingRight() ? 1.0f : -1.0f);
        // The step made only that share of the travel it assumed.
        if (!Stride.Crouched) StepTravel = std::max(StepTravel - std::abs(Stride.Travel) * (1.0f - Share), 0.0f);
        Shown = anim::blendPoses(ShownStill, Shown, Share);
        ShownLegs = anim::blendPoses(ShownLegsStill, ShownLegs, Share);
    }
    LastPlannedTravel = Body.getController().getPlannedTravel();
    Body.applyControl(Dt);
    checkPelvisTrack();
}

bool Fighter::isHittable() const { return State != FighterState::KnockedDown && State != FighterState::KnockedOut; }

HitOutcome Fighter::takeHit(const physics::HitEvent& Hit, const MoveDef& Attack, float PowerScale, float Direction,
                            bool JammedStrike) {
    const bool OnShield = Body.isOnShield(Hit.Point, Rules->Tuning.ShieldHitMargin);
    const HitInput Input{
        .Impulse = Hit.Impulse,
        .Part = Hit.Victim.Part,
        .VictimMass = Body.getTotalMass(),
        .Armor = Profile.Parts[static_cast<size_t>(Hit.Victim.Part)].Armor,
        .Poise = Profile.Poise,
        .Buildup = Buildup,
        .Guard = State == FighterState::Blocking ? std::optional(Guard) : std::nullopt,
        .Block = &Block,
        .OnShield = OnShield,
        .MoveDamage = Attack.Damage,
        .PowerScale = PowerScale,
        .MinReaction = JammedStrike ? ReactionLevel::None : Attack.MinReaction,
    };
    const HitOutcome Outcome = resolveHit(Rules->Reactions, Input);
    Hp = std::max(0.0f, Hp - Outcome.Damage);
    Buildup += Outcome.BuildupAdded;
    if (Outcome.Blocked) spendStamina(Outcome.BlockStamina);
    LastHit = HitRecord{.MoveId = Attack.Id, .Part = Hit.Victim.Part, .Outcome = Outcome, .OnShield = OnShield};
    react(Outcome.Reaction, Hit.Impulse, Direction, Hit.Point);
    return Outcome;
}

void Fighter::onStrikeLanded(bool Clean) {
    AttackLanded = true;
    AttackHitClean = Clean;
}

void Fighter::onPosedStop(std::optional<float> StrikeKept) {
    // In any phase: nothing passes through the opponent. A contact in the
    // startup jams the attack.
    if (!getMove() || !StrikeKept) return;
    const float Kept = *StrikeKept;
    const CombatTuning& Tuning = Rules->Tuning;
    const bool Startup = AttackTime < AttackClip->ActiveBeginSec;
    if (Contact != ContactStage::None || AttackTimeBefore >= AttackClip->ActiveEndSec) return;

    // The first stop: the clip goes back to the time of the contact (clip
    // time advances evenly within a step) and holds there. A contact of the
    // striking phase stays in it, so one that was too slow to be a hit can
    // still land.
    const float Stopped = AttackTimeBefore + (AttackTime - AttackTimeBefore) * Kept;
    AttackTime = Startup ? Stopped : std::clamp(Stopped, AttackClip->ActiveBeginSec, AttackClip->ActiveEndSec);
    Contact = ContactStage::Holding;
    ContactClipSec = AttackTime;
    // The body does not lunge on through what the strike ran into.
    if (Lunge.Moving) stopPelvisTrack("stopped at the opponent");
    ContactHoldLeftSec = Tuning.ContactHoldSec;
    Jammed = Startup;
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} {} at the opponent (clip {:.2f} s)", Body.getFighterIndex() + 1,
                                    Move->Id, Startup ? "jammed in the startup" : "stopped", AttackTime));
    }
}

bool Fighter::takeExhaustedNotice() { return std::exchange(ExhaustedNotice, false); }

std::string_view Fighter::getMoveId() const {
    const MoveDef* Current = getMove();
    return Current ? std::string_view(Current->Id) : std::string_view();
}

const anim::Clip& Fighter::getBlockClip() const { return Rules->Clips.get(Block.getClip(Guard)); }

float Fighter::getPowerScale(const MoveDef& Attack) const {
    return Attack.UsesWeapon && StrikeWeapon ? Weapons[*StrikeWeapon].Props.PowerScale : 1.0f;
}

std::string Fighter::describeClip() const {
    const anim::Clip* Top = getTopClip();
    const anim::Clip& Playing = Top ? *Top : Rules->Clips.get(Walk.isPlaying() ? std::string_view(clips::Walk) : std::string_view(StanceName));
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
    Body.getWeaponTransforms(View.Weapons);
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
        // The moveset, the input of the last step (PreviousCmd is that
        // step's commands by now), the move it chose and the block.
        debug::setPanel(std::format("{} moveset", Name), describeMoveSet());
        debug::setPanel(std::format("{} input", Name), describeInput());
        debug::setPanel(std::format("{} move", Name), describeSelected());
        debug::setPanel(std::format("{} block", Name), describeBlock());
        const ReactionTable& Table = Rules->Reactions;
        debug::setPanel(std::format("{} poise", Name),
                        std::format("buildup {:.2f}, poise {:.2f}: thresholds x{:.2f}", Buildup, Profile.Poise,
                                    getThresholdScale(Table, Profile.Poise, Buildup)));
        if (LastHit) {
            const HitOutcome& Outcome = LastHit->Outcome;
            debug::setPanel(std::format("{} last hit", Name),
                            std::format("{} -> {}: {:.2f} m/s -> {}, {:.1f} dmg{}{}", LastHit->MoveId,
                                        getBodyPartName(LastHit->Part), Outcome.Strength,
                                        getReactionLevelName(Outcome.Reaction), Outcome.Damage,
                                        Outcome.Blocked ? " (blocked)" : "", LastHit->OnShield ? " on the shield" : ""));
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
        debug::setPanel(std::format("{} lunge", Name), describePelvisTrack());
        drawPelvisTrack();
        debug::setPanel(std::format("{} legs", Name), describeLegs());
        debug::setPanel(std::format("{} upper", Name), describeUpper());
        debug::setPanel(std::format("{} stride", Name), describeStride());
        if (Step.isActive()) Step.drawDebug(LegTarget, Body);
        if (RestStep.isActive()) RestStep.drawDebug(RestTarget, Body);
        debug::setPanel(std::format("{} blend", Name), describeBlends());
        // "P1 facing" (and a pending turn) is the rig's panel line.
    }
}

std::string Fighter::describeMoveSet() const {
    std::string Text = Set->Id;
    if (!Set->Pair.empty()) Text += std::format(" (pair {})", joinNames(Set->Pair, " + "));
    for (const MoveSet* Parent = Rules->Moves.findSet(Set->Inherit); Parent;
         Parent = Rules->Moves.findSet(Parent->Inherit)) {
        Text += " < " + Parent->Id;
        if (Parent->Inherit.empty()) break;
    }
    for (const HandWeapon& Held : Weapons) {
        Text += std::format("; {} in {}", Held.ItemName, getBodyPartName(Held.Part));
    }
    return Text;
}

std::string Fighter::describeInput() const {
    const ButtonSet Held = getHeldButtons(PreviousCmd) | Presses.getButtons();
    const InputDirection Direction = getInputDirection(PreviousCmd, Body.isFacingRight());
    std::string Text = Held.isEmpty() ? std::string(getInputDirectionName(Direction))
                                      : formatMoveInput({.Direction = Direction, .Buttons = Held});
    if (WaitingForCombo) {
        Text += std::format(" (waiting for a combination {:.2f}/{:.2f} s)", Presses.getAgeSec(),
                            Rules->Moves.getInputRules().ComboWindowSec);
    }
    return Text;
}

std::string Fighter::describeSelected() const {
    if (!Selected) return "-";
    // The set of the line: this set or the parent it came from.
    std::string_view From = "?";
    for (const MoveSet* Current = Set; Current; Current = Rules->Moves.findSet(Current->Inherit)) {
        const auto& Entries = Current->Entries;
        if (!Entries.empty() && Selected >= Entries.data() && Selected < Entries.data() + Entries.size()) {
            From = Current->Id;
            break;
        }
        if (Current->Inherit.empty()) break;
    }
    std::string Text = std::format("{} -> {} ({})", formatMoveInput(Selected->Input), Selected->MoveId, From);
    if (getMove() && StrikeWeapon) {
        Text += std::format(", {}{}", Weapons[*StrikeWeapon].ItemName, StrikeOtherHand ? " in the other hand" : "");
    }
    if (ChainRequest) Text += std::format(", chain to {} asked", ChainRequest->Id);
    return Text;
}

std::string Fighter::describeBlock() const {
    std::string Covers;
    for (const BodyPart Part : Block.Covers[static_cast<size_t>(Guard)]) {
        Covers += Covers.empty() ? "" : " ";
        Covers += getBodyPartName(Part);
    }
    return std::format("dmg x{:.2f}, max {}, stamina x{:.2f}; {}: {}, covers {}", Block.DamageScale,
                       getReactionLevelName(Block.MaxLevel), Block.StaminaScale, getBlockZoneName(Guard),
                       Block.getClip(Guard), Covers);
}

bool Fighter::isFree() const {
    return State == FighterState::Idle || State == FighterState::Walking || State == FighterState::Crouching ||
           State == FighterState::Blocking;
}

void Fighter::setState(FighterState Next) {
    if (Next == State) return;
    if (State == FighterState::Attacking) {
        // A reaction or a fall stops the lunge at once (a finished clip's
        // track is over by now).
        if (Lunge.Moving) {
            stopPelvisTrack(Next == FighterState::Reacting ? "interrupted by a reaction"
                            : Next == FighterState::Idle   ? "interrupted"
                                                           : "interrupted by a fall");
        }
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
        // recovers: the clip jumps to its recovery (its timing stays the
        // clip's), but the pose goes back the way it came, from the contact
        // pose to the clip's start (getTopClipTime()): the clip's own
        // recovery starts from the extended pose, deeper in the opponent.
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
    // Before a chain or the end: the last step of the track still moves.
    followPelvisTrack();
    // A press (not a held button) of a move the current one chains into asks
    // for it; it is kept until the cancel window. The buttons pressed within
    // the combo window count together: a later press may turn the request
    // into a combination.
    if (!getNewlyPressed(Cmd, PreviousCmd).isEmpty()) {
        const ButtonSet Recent = Presses.getButtons();
        const MoveSetEntry* Entry = Rules->Moves.findEntry(*Set, getInputDirection(Cmd, Body.isFacingRight()),
                                                           Recent, getHeldButtons(Cmd) | Recent);
        const MoveDef* Next = Entry ? Rules->Moves.findMove(Entry->MoveId) : nullptr;
        if (Next && Move->canChainTo(Next->Id)) {
            ChainRequest = Next;
            ChainEntry = Entry;
        }
    }
    if (AttackTime >= AttackClip->ActiveEndSec) {
        RecoverySec += Dt;
        const CombatTuning& Tuning = Rules->Tuning;
        const bool CanChain = AttackHitClean && ChainRequest && ChainLength < Tuning.MaxChainLength &&
                              RecoverySec <= Tuning.ChainWindowSec;
        if (CanChain) {
            const MoveDef* Next = ChainRequest;
            Selected = ChainEntry;
            startMove(*Next, Around, ChainLength + 1);
            return Next;
        }
    }
    if (AttackClip->isFinishedAt(AttackTime)) {
        // The presses of the attack were for a chain; a held button repeats
        // the attack by itself.
        Presses.clear();
        setState(FighterState::Idle);
    }
    return nullptr;
}

const MoveDef* Fighter::chooseFreeState(const PlayerCommands& Cmd, const Surroundings& Around, float Dt) {
    // A zone the moveset does not guard: the block button does nothing there.
    if (const std::optional<BlockZone> Zone = getBlockZone(Cmd, Body.isFacingRight()); Zone && Block.hasGuard(*Zone)) {
        if (State != FighterState::Blocking || *Zone != Guard) StateSec = 0.0f;
        Guard = *Zone;
        PendingAttack = nullptr;
        // The block wins over attacking: a press while blocking is dropped.
        Presses.clear();
        WaitingForCombo = false;
        setState(FighterState::Blocking);
        return nullptr;
    }
    // A strike pressed while crouched starts once the fighter stood up (a
    // block drops it, above). Down still held crouches again after it.
    if (PendingAttack) {
        StandUpLeftSec -= Dt;
        if (StandUpLeftSec > 0.0f) return nullptr;
        const MoveDef* Next = std::exchange(PendingAttack, nullptr);
        startMove(*Next, Around, 1);
        return Next;
    }
    // Holding attack buttons repeats the attack. A press that may still
    // become a combination ("Light+Heavy") waits for the rest of it within
    // the combo window; the buttons pressed in it count as held.
    const ButtonSet Recent = Presses.getButtons();
    const ButtonSet Held = getHeldButtons(Cmd) | Recent;
    const InputDirection Direction = getInputDirection(Cmd, Body.isFacingRight());
    WaitingForCombo = Presses.getAgeSec() < Rules->Moves.getInputRules().ComboWindowSec &&
                      Rules->Moves.canGrowCombo(*Set, Direction, Recent);
    const MoveSetEntry* Entry =
        Held.isEmpty() || WaitingForCombo ? nullptr : Rules->Moves.findEntry(*Set, Direction, Held, Held);
    if (Entry) Selected = Entry;
    if (const MoveDef* Next = Entry ? Rules->Moves.findMove(Entry->MoveId) : nullptr) {
        // Crouched, a move mapped to a downward direction (a sword's low cut)
        // starts at once; the rest stand up first.
        if (State == FighterState::Crouching && !isDownward(Entry->Input.Direction) &&
            Rules->Tuning.CrouchStandUpSec > 0.0f) {
            PendingAttack = Next;
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
    // A weapon move strikes with the weapon its clip's strikers hold, or
    // with the other arm when the weapon is in the other hand.
    StrikeWeapon.reset();
    StrikeOtherHand = false;
    if (Next.UsesWeapon) chooseStrikeWeapon(Authored);
    const anim::Clip& Clip = getPlayed(Authored, StrikeOtherHand);
    // A move always starts; without enough stamina it empties it and the
    // fighter is exhausted, so the move itself is already slow.
    spendStamina(Next.Stamina);

    float Rate = Profile.AttackSpeedScale;
    if (Next.UsesWeapon && StrikeWeapon) Rate *= Weapons[*StrikeWeapon].Props.SpeedScale;
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
    ChainRequest = nullptr;
    ChainEntry = nullptr;
    // The pelvis track starts from where the pelvis is; a chain drops what
    // is left of the track of the move before (it is in its recovery).
    Lunge = PelvisTrack{.ClipName = Clip.PelvisTrack.empty() ? std::string() : Clip.Name,
                        .StartX = Body.getController().getPositionX(),
                        .Facing = Lunge.Facing,
                        .Moving = !Clip.PelvisTrack.empty()};
    // The buttons that started it are spent: they do not start another.
    Presses.clear();
    WaitingForCombo = false;
}

float Fighter::planWalking(const PlayerCommands& Cmd) {
    const CombatTuning& Tuning = Rules->Tuning;
    Stride = {};
    if (Body.getPosture() != rig::Posture::Standing) {
        // Down or getting up, the legs come back into the stance.
        RestFront = BodyPart::FootL;
        Walk.settle(RestFront);
        CrouchWalk.settle();
        clearRest();
        return 0.0f;
    }
    const bool Crouched = State == FighterState::Crouching;
    if (Crouched) Settle.reset();
    if (!Crouched) CrouchWalk.settle();
    Stride.Crouched = Crouched;
    const bool AttackAllowsMove = State == FighterState::Attacking && AttackClip->AllowMove && !AttackFromCrouch;
    const bool BlockAllowsMove = State == FighterState::Blocking && getBlockClip().AllowMove;
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
    if (!WantsToMove && Settle) {
        // Settling: the pelvis glides to its place in the stance, braking to
        // a stop there.
        const float Brake = std::sqrt(2.0f * Control.WalkDeceleration * std::abs(Settle->Left));
        const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
        return std::copysign(std::min(Tuning.StopSettleSpeed, Brake), Settle->Left) * Facing;
    }
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
    const float CycleSpeed = Body.getStrideSpeed() * (Crouched ? Tuning.CrouchWalkSpeedScale : 1.0f);
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
        const bool Released = !Crouched && !WalkHeld && Cycle.getMode() == LegCycle::Mode::Walking;
        if (Released) beginSettle();
        if (Settle && !Crouched) {
            settleLegs(Dt);
            return;
        }
        stopLegs(Cycle, Crouched, Dt);
        return;
    }
    if (!Crouched) leaveRest();
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
    // A new step starts at each double support.
    StepTravel = Cycle.isInSpan() ? 0.0f : StepTravel + std::abs(Travel);
    StepLength = StepTravel + Cycle.getLeftToSpanAhead() * CycleSpeed;
}

void Fighter::beginSettle() {
    const CombatTuning& Tuning = Rules->Tuning;
    const rig::LegStance Now = Body.measureLegsNow();
    const float PlantHeight = Body.getControl().FootPlantHeight;
    const auto getOther = [](BodyPart Foot) { return Foot == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL; };
    // The foot that stays: of the swing foot of the step going on (set down
    // where it is) and the standing one, the one that has the pelvis's place
    // in the stance nearest to where the pelvis brakes to a stop anyway, so
    // that it does not go back against its own momentum.
    std::optional<BodyPart> Lifted;
    for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
        const float Height = Now.getFoot(Foot).SoleHeight;
        if (Height > PlantHeight && (!Lifted || Height > Now.getFoot(*Lifted).SoleHeight)) Lifted = Foot;
    }
    const float Heading = Walk.getDirection() > 0.0f ? 1.0f : -1.0f;
    const auto getFront = [&](BodyPart Stays) {
        const float Ahead = Now.getFoot(Stays).Ankle.X - Now.getFoot(getOther(Stays)).Ankle.X;
        const bool InFront = std::abs(Ahead) > ReachToleranceM ? Ahead > 0.0f : Heading > 0.0f;
        return InFront ? Stays : getOther(Stays);
    };
    const auto getLeft = [&](BodyPart Stays) {
        const rig::LegStance Stance = Body.measureLegs(getStanceLegs(getFront(Stays)).Angles);
        return Now.getFoot(Stays).Ankle.X - Stance.getFoot(Stays).Ankle.X;
    };
    const rig::ControlParams& Control = Body.getControl();
    const float Velocity = Body.getController().getWalkVelocity() * (Body.isFacingRight() ? 1.0f : -1.0f);
    const float Braking = Velocity * std::abs(Velocity) / (2.0f * Control.WalkDeceleration);
    const BodyPart Stays = std::abs(getLeft(BodyPart::FootL) - Braking) <= std::abs(getLeft(BodyPart::FootR) - Braking)
                               ? BodyPart::FootL
                               : BodyPart::FootR;
    const BodyPart Other = getOther(Stays);
    RestFront = getFront(Stays);
    const anim::Pose StanceLegs = getStanceLegs(RestFront);
    const rig::LegStance Stance = Body.measureLegs(StanceLegs.Angles);
    const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
    Settle = StanceSettle{.Stays = Stays,
                          .StaysX = Body.getController().getPositionX() + Now.getFoot(Stays).Ankle.X * Facing,
                          .StaysAt = Stance.getFoot(Stays).Ankle.X,
                          .Spread = Stance.getFoot(Other).Ankle.X - Stance.getFoot(Stays).Ankle.X};
    SettleDone = 0.0f;
    // The staying foot comes down where it is, flat as in the stance; the
    // other one goes to the stance's spread from it.
    const rig::FootPlacement& Flat = Stance.getFoot(Stays);
    const bool SetDown = Lifted == Stays;
    FootLanding Rest{.SetDown = SetDown ? Lifted : std::nullopt, .Feet = Stance};
    Rest.Feet.getFoot(Stays) = {.Ankle = {Now.getFoot(Stays).Ankle.X, Flat.Ankle.Y - Flat.SoleHeight},
                                .Angle = Flat.Angle};
    if (!SetDown) {
        rig::FootPlacement Planted = Now.getFoot(Stays);
        Planted.Ankle.Y -= std::min(Planted.SoleHeight, 0.0f);
        Planted.SoleHeight = std::max(Planted.SoleHeight, 0.0f);
        Planted.Planted = false;
        Rest.Feet.getFoot(Stays) = Planted;
    }
    RestLanding = Rest;
    placeSettleFeet();

    // The legs are the stance's from now on; the steps take them there from
    // where they stand, so nothing fades. The walk holds its phase until the
    // settle is over: walking again meanwhile goes on with the step it
    // stopped in.
    Walk.rest();
    ShownSource = LegSource::Stance;
    Anchor.reset();
    LegFade.cancel();
    LegFadeByTravel = false;
    FeetSettling = true;
    StepTravel = 0.0f;
    // The swing foot lands first; the other foot steps after it (or, in the
    // air, at once). Both go to places that move with the pelvis (as the
    // feet of a clip do): the staying foot to its spot on the floor, the
    // other one to the stance's spread from it. Any way off: the stance is
    // to be the stance.
    const rig::LegStance Target = Body.measureLegs(applyLanding(StanceLegs).Angles);
    LegStepTuning StepTuning = Tuning.LegStep;
    StepTuning.MinDistance = ReachToleranceM;
    const bool OtherOff = Lifted == Other ||
                          std::abs(Now.getFoot(Other).Ankle.X - Target.getFoot(Other).Ankle.X) > ReachToleranceM;
    const float Sec = Tuning.StopSettleSec + (SetDown ? Tuning.LegStep.RestSec : 0.0f);
    const auto isMoved = [&](BodyPart Foot) { return Foot == Other ? OtherOff : SetDown; };
    RestStep = LegStep::plan(Now, Target, isMoved(BodyPart::FootL), isMoved(BodyPart::FootR), false, Sec, StepTuning);
    RestStepFresh = RestStep.isActive();
    Body.dropLiftedFootOffsets();
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} settles around {}{}: pelvis {:.2f} m to go{}", Body.getFighterIndex() + 1,
                                    getBodyPartName(Stays), SetDown ? " (set down)" : "", Settle->Left,
                                    OtherOff ? std::format(", {} steps", getBodyPartName(Other)) : ""));
    }
}

void Fighter::placeSettleFeet() {
    // The staying foot where it stands, or lands, on the floor (a planted
    // one goes along with a push).
    rig::LegStance& Feet = RestLanding->Feet;
    rig::FootPlacement& Stays = Feet.getFoot(Settle->Stays);
    const float PelvisX = Body.getController().getPositionX();
    const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
    const rig::FootPlacement Now = Body.measureLegsNow().getFoot(Settle->Stays);
    if (Now.Planted) Settle->StaysX = PelvisX + Now.Ankle.X * Facing;
    Stays.Ankle.X = (Settle->StaysX - PelvisX) * Facing;
    // The pelvis's place in the stance over it, but not into a wall, nor
    // closer to the opponent than the spacing keeps them: there the stance
    // stays narrower. The other foot is where the stance has it from there.
    float Place = Settle->StaysX - Settle->StaysAt * Facing;
    Place = std::clamp(Place, -Rules->PelvisLimitX, Rules->PelvisLimitX);
    if (!OpponentDown) {
        const float Side = OpponentX > PelvisX ? 1.0f : -1.0f;
        const float Nearest = OpponentX - Side * 2.0f * Rules->Tuning.BodyHalfWidth;
        if ((Place - Nearest) * Side > 0.0f) Place = Nearest;
    }
    Settle->Left = (Place - PelvisX) * Facing;
    const BodyPart Other = Settle->Stays == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
    Feet.getFoot(Other).Ankle.X = Settle->Left + Settle->StaysAt + Settle->Spread;
}

void Fighter::settleLegs(float Dt) {
    rig::PelvisController& Motion = Body.getController();
    const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
    // The walk goes no further than the pelvis's place.
    if (Motion.getWalkVelocity() * Facing * Settle->Left > 0.0f) Motion.capWalkTravel(std::abs(Settle->Left), Dt);
    SettleDone += std::abs(Motion.getPlannedTravel());
    Body.keepFeetPlanted();
    Settle->Sec += Dt;
    const bool Stepping = RestStep.isActive();
    const bool Gets = std::abs(Motion.getVelocity()) >= SettleStuckSpeed;
    Settle->StuckSec = Stepping || Gets ? 0.0f : Settle->StuckSec + Dt;
    const bool Pushed = std::abs(Motion.getKnockback()) > CalmKnockback;
    const bool There = std::abs(Settle->Left) <= SettleEndM;
    const bool Stuck = Settle->StuckSec >= SettleStuckSec || Settle->Sec >= SettleMaxSec || Pushed;
    if (Stepping || !(There || Stuck)) return;
    // At its place over the staying foot, the legs stand in the stance as
    // it is; held off it (the opponent, a wall), the feet stay where they
    // stand around the pelvis.
    const float StaysOff = RestLanding->Feet.getFoot(Settle->Stays).Ankle.X - Settle->StaysAt;
    if (There && std::abs(StaysOff) <= ReachToleranceM) RestLanding.reset();
    Walk.settle(RestFront);
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} settled after {:.2f} m{}", Body.getFighterIndex() + 1, SettleDone,
                                    RestLanding ? std::format(", {:.2f} m off the stance", StaysOff) : ""));
    }
    Settle.reset();
}

void Fighter::anchorStep() {
    const std::optional<size_t> Found = Walk.findStep(Walk.getTime());
    if (!Found) return;
    const float Heading = Walk.getDirection() > 0.0f ? 1.0f : -1.0f;
    const bool FacingRight = Body.isFacingRight();
    if (Anchor && Anchor->Step == *Found && Anchor->Heading == Heading && Anchor->FacingRight == FacingRight) {
        // The standing foot is where the rig holds it: a push of the bodies
        // takes planted feet along (rig::Rig::pushBody).
        const CycleStep& Current = Walk.getSteps()[*Found];
        const BodyPart Standing = Current.Swing == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
        if (Body.isFootLocked(Standing)) {
            const float PelvisX = WalkOdometer;
            Anchor->StandX = PelvisX + Body.measureLegsNow().getFoot(Standing).Ankle.X;
        }
        return;
    }
    // Anchored where the body is now: at the phase before this tick's step.
    const CycleStep& Each = Walk.getSteps()[*Found];
    const BodyPart Stand = Each.Swing == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
    const float PelvisX = WalkOdometer;
    const rig::LegStance Now = Body.measureLegsNow();
    Anchor = StrideAnchor{.Step = *Found,
                          .Heading = Heading,
                          .Share = std::clamp(Walk.getStepShare(*Found, Walk.getStepFromTime()), 0.0f, 1.0f),
                          .PelvisX = PelvisX,
                          .SwingX = PelvisX + Now.getFoot(Each.Swing).Ankle.X,
                          .StandX = PelvisX + Now.getFoot(Stand).Ankle.X,
                          .FacingRight = FacingRight};
}

anim::Pose Fighter::placeStepFeet(const anim::Pose& Legs, float TimeSec, float PelvisX) const {
    if (!Anchor) return Legs;
    const CycleStep& Each = Walk.getSteps()[Anchor->Step];
    const BodyPart Stand = Each.Swing == BodyPart::FootL ? BodyPart::FootR : BodyPart::FootL;
    const float Length = (Each.EndSec - Each.BeginSec) * Body.getStrideSpeed();
    // The share of the step's swing in the air: the foot moves along the
    // floor only between the clip's lift-off and landing.
    const auto getAir = [&](float Share) {
        return std::clamp((Share - Each.LiftShare) / (Each.LandShare - Each.LiftShare), 0.0f, 1.0f);
    };
    // The swing foot goes from where it was anchored to where the clip
    // lands it at the end of the step it heads to (the pelvis then plus the
    // clip's ankle there), in proportion to the air share.
    const float EndShare = Anchor->Heading > 0.0f ? 1.0f : 0.0f;
    const float EndPelvis = Anchor->PelvisX + (EndShare - Anchor->Share) * Length;
    const float EndX = EndPelvis + (Anchor->Heading > 0.0f ? Each.SwingEndX : Each.SwingBeginX);
    const float Share = Walk.getStepShare(Anchor->Step, TimeSec);
    const float From = getAir(Anchor->Share);
    const float To = getAir(EndShare);
    float SwingX = EndX;
    float Along = 1.0f;
    if (std::abs(To - From) > 1e-4f) {
        Along = std::clamp((getAir(Share) - From) / (To - From), 0.0f, 1.0f);
        SwingX = Anchor->SwingX + (EndX - Anchor->SwingX) * Along;
    } else if (std::abs(getAir(Share) - To) > 1e-4f) {
        Along = 0.0f;
        SwingX = Anchor->SwingX;
    }

    // Heights and foot angles from the clip; the standing foot on the floor.
    // The swing foot is lifted WalkLiftScale of the clip's height above the
    // clearance, less on a
    // step shorter than the clip's: its travel against what the clip's foot
    // would travel over the rest of the step (so that a step anchored in the
    // air does not drop the foot).
    const float ClipTravel = std::abs(Length + Each.SwingEndX - Each.SwingBeginX) * std::abs(To - From);
    const float StepShare =
        ClipTravel > 1e-4f ? std::min(std::abs(EndX - Anchor->SwingX) / ClipTravel, 1.0f) : 1.0f;
    const float LiftScale = Rules->Tuning.WalkLiftScale * StepShare;
    const rig::LegStance Clip = Body.measureLegs(Legs.Angles);
    anim::Pose Result = Legs;
    const rig::FootPlacement& Standing = Clip.getFoot(Stand);
    const rig::FootPlacement& Swinging = Clip.getFoot(Each.Swing);
    Body.reachFoot(Result.Angles, Stand, Clip.PelvisHeight,
                   {Anchor->StandX - PelvisX, Standing.Ankle.Y - Standing.SoleHeight}, Standing.Angle);
    // A swing foot on its way is off the floor, so that the rig does not
    // take it for a standing one and hold it.
    const bool OnItsWay = Along > 0.0f && Along < 1.0f;
    const float Clearance = Body.getControl().FootPlantHeight * SwingClearance;
    // Only the height above the clearance is scaled: lower, a foot in the
    // air could pass for a planted one.
    const float Lowered = std::max(Swinging.SoleHeight - Clearance, 0.0f) * (1.0f - LiftScale);
    const float Raise = OnItsWay ? std::max(Clearance - Swinging.SoleHeight, 0.0f) : 0.0f;
    Body.reachFoot(Result.Angles, Each.Swing, Clip.PelvisHeight, {SwingX - PelvisX, Swinging.Ankle.Y - Lowered + Raise},
                   Swinging.Angle);
    return Result;
}

bool Fighter::canStandAt(const anim::Pose& Legs, float PelvisHeight, BodyPart Foot, const rig::FootPlacement& Place,
                         bool KneeCap) const {
    PerBodyPart<float> Angles = Legs.Angles;
    Body.reachFoot(Angles, Foot, PelvisHeight, Place.Ankle, Place.Angle);
    const rig::FootPlacement Reached = Body.measureLegs(Angles).getFoot(Foot);
    const auto Knee = static_cast<size_t>(Foot == BodyPart::FootL ? BodyPart::ShinL : BodyPart::ShinR);
    const bool KneeFits =
        !KneeCap || std::abs(Angles[Knee]) <= std::abs(Legs.Angles[Knee]) + Body.getControl().KneeExtraBend;
    return KneeFits && std::abs(Reached.Ankle.X - Place.Ankle.X) <= ReachToleranceM &&
           std::abs(Reached.Ankle.Y - Place.Ankle.Y) <= ReachToleranceM;
}

anim::Pose Fighter::applyLanding(const anim::Pose& Legs) const {
    if (!RestLanding) return Legs;
    anim::Pose Result = Legs;
    const rig::LegStance& Feet = RestLanding->Feet;
    for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
        Body.reachFoot(Result.Angles, Foot, Feet.PelvisHeight, Feet.getFoot(Foot).Ankle, Feet.getFoot(Foot).Angle);
    }
    return Result;
}

void Fighter::leaveRest() {
    Settle.reset();
    if (!RestLanding && !RestStep.isActive()) return;
    // The legs go from what is shown (the landed foot, a re-step) into the
    // walk cycle: the set-down foot lifts again and goes on with its step.
    RestLanding.reset();
    RestStep.cancel();
    // Over travel, not time: a short press moves the legs only a little of
    // the way, as a step would.
    const float Distance = Rules->Tuning.StopResumeDistance;
    LegFade.begin(anim::selectJoints(Shown, anim::getLegJoints()), Distance);
    LegBlend = {.From = PoseKind::Stance, .To = PoseKind::Walk, .Sec = Distance};
    LegFadeByTravel = LegFade.isActive();
}

void Fighter::clearRest() {
    RestLanding.reset();
    RestStep.cancel();
    Settle.reset();
}

void Fighter::updateRestStep(TargetPoses& Target, float Dt) {
    // A lunge carries the body over the planted feet: they hold their place
    // as far as the legs reach (no slide, no step of the rig's own), and
    // only the leading foot steps (planLungeStep()).
    const bool Lunging = Lunge.Moving || Lunge.Planned != 0.0f;
    if (Lunging) {
        Body.keepFeetPlanted();
        LungeLegs = true;
    }
    const bool Resting = Body.getPosture() == rig::Posture::Standing && !Walk.isHeld() &&
                         (!Walk.isEngaged() || Walk.getMode() == LegCycle::Mode::Still);
    if (!Resting) {
        RestStep.cancel();
        return;
    }
    const rig::PelvisController& Motion = Body.getController();
    const bool Calm = Motion.getWalkVelocity() == 0.0f && std::abs(Motion.getKnockback()) < CalmKnockback;
    if (!RestStep.isActive() && Calm && !Settle && Lunging) {
        planLungeStep(Body.measureLegsNow(), Body.measureLegs(Target.Moving.Angles));
    } else if (!RestStep.isActive() && Calm && !Settle) {
        // A planted foot far from where the rest pose has it steps there.
        const LegStepTuning& Tuning = Rules->Tuning.LegStep;
        const rig::LegStance Now = Body.measureLegsNow();
        const rig::LegStance Wanted = Body.measureLegs(Target.Moving.Angles);
        // Not towards an opponent this close: a foot could come down in its
        // legs (the rig holds the feet where they stand meanwhile).
        const bool TowardsOpponent = std::ranges::any_of(std::array{BodyPart::FootL, BodyPart::FootR}, [&](BodyPart Foot) {
            return Wanted.getFoot(Foot).Ankle.X - Now.getFoot(Foot).Ankle.X > Tuning.MinDistance;
        });
        const bool Clear = !TowardsOpponent || OpponentGap >= Tuning.RestepClearance;
        // The feet a lunge left are back in the stance: the pelvis no longer
        // goes down deeper for them.
        const bool Home = std::ranges::all_of(std::array{BodyPart::FootL, BodyPart::FootR}, [&](BodyPart Foot) {
            return std::abs(Now.getFoot(Foot).Ankle.X - Wanted.getFoot(Foot).Ankle.X) <= Tuning.RestepDistance;
        });
        if (Home) LungeLegs = false;
        for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
            if (!Clear) break;
            const float Off = Now.getFoot(Foot).Ankle.X - Wanted.getFoot(Foot).Ankle.X;
            if (!Now.getFoot(Foot).Planted || std::abs(Off) <= Tuning.RestepDistance) continue;
            RestStep = LegStep::plan(Now, Wanted, false, false, false, Tuning.RestSec, Tuning);
            RestStepFresh = RestStep.isActive();
            if (!RestStepFresh) break;
            Body.dropLiftedFootOffsets();
            ++Resteps;
            LastRestep = std::format("{} {:.2f} m", getBodyPartName(Foot), std::abs(Off));
            if constexpr (FIGHTER_DEBUG) {
                debug::logEvent(std::format("P{} re-steps {}: {:.2f} m off the pose", Body.getFighterIndex() + 1,
                                            getBodyPartName(Foot), std::abs(Off)));
            }
            break;
        }
    }
    if (!RestStep.isActive()) return;
    // The step's first tick holds the feet where they stand; it moves from
    // the next one.
    if (!std::exchange(RestStepFresh, false)) RestStep.advance(Dt, Body.measureLegsNow());
    RestTarget = Body.measureLegs(Target.Moving.Angles);
    RestStep.apply(Target.Moving.Angles, Body);
    RestStep.apply(Target.Still.Angles, Body);
    Body.keepFeetPlanted();
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
        if (Cycle.getMode() == LegCycle::Mode::Walking) chooseStop(Cycle, Crouched);
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

anim::Pose Fighter::getStancePose() const {
    // The wrist a clip leaves out: the stance's, else the weapon's own
    // default (the item's angle, else the rig's), so that a clip that sets
    // it blends in from there.
    anim::Pose Stance = anim::sampleClip(Rules->Clips.get(StanceName), 0.0f);
    if (const std::optional<float> Default = Body.getDefaultWristAngle(); Default && !Stance.HasWeapon) {
        Stance.setWeaponAngle(*Default);
    }
    return Stance;
}

Fighter::TargetPoses Fighter::buildTargetPose(const anim::Clip* Top, bool TopChanged, float Dt) {
    const ClipLibrary& Clips = Rules->Clips;
    const anim::Pose Stance = getStancePose();
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
        updateRestStep(Result, Dt);
        return Result;
    }
    if (TopChanged) {
        // Steps lead from the resting legs into a leg action. From one leg
        // action into the next (the crouch into the low block)
        // the legs are already the action's: the change blends as authored.
        // From the stance with the same foot in front the legs are in the
        // pose the action is authored from: no steps either.
        const bool FromLegAction = ShownTop && anim::usesLegs(*ShownTop);
        const BodyPart ActionFront = LegsMirrored ? BodyPart::FootR : BodyPart::FootL;
        const bool FromItsStance = ShownSource == LegSource::Stance && RestFront == ActionFront;
        if (FromLegAction || FromItsStance) {
            Step.cancel();
            LegsStepped = false;
        } else {
            startLegStep(*Top, Result.Moving);
        }
    }
    const bool Stepping = Step.isActive();
    // The step tick holds the feet where they stand (the rig takes them
    // there without an offset); the steps start in the next one.
    if (!TopChanged) Step.advance(Dt, Body.measureLegsNow());
    LegTarget = Body.measureLegs(Result.Moving.Angles);
    Step.apply(Result.Moving.Angles, Body);
    Step.apply(Result.Still.Angles, Body);
    // The rig would take a planted foot held off its old pose for one left
    // behind by a push and step it back: the feet stay where the steps put
    // them.
    // A lunge in a leg action: the supporting foot holds its place as far as
    // the leg reaches, as in updateRestStep().
    if (Stepping || Lunge.Moving || Lunge.Planned != 0.0f) Body.keepFeetPlanted();
    return Result;
}

void Fighter::chooseStop(LegCycle& Cycle, bool Crouched) {
    const std::vector<StopTarget> Targets = Cycle.getStopTargets();
    if (Targets.empty()) return;
    // Played on without travel, the cycle moves the planted foot relative to
    // the pelvis, and the rig holds it where it stands: the target where the
    // cycle has the planted feet closest to where they are leaves the legs
    // closest to the cycle's own rest pose (and as wide).
    const rig::LegStance Now = Body.measureLegsNow();
    const auto getMismatch = [&](const StopTarget& Target) {
        const rig::LegStance Then = Body.measureLegs(getCyclePose(Crouched, Target.TimeSec).Angles);
        float Mismatch = 0.0f;
        for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
            if (Now.getFoot(Foot).Planted) Mismatch += std::abs(Then.getFoot(Foot).Ankle.X - Now.getFoot(Foot).Ankle.X);
        }
        return Mismatch;
    };
    const StopTarget* Best = nullptr;
    float BestMismatch = 0.0f;
    for (const StopTarget& Target : Targets) {
        const float Mismatch = getMismatch(Target);
        const bool Tie = Best && std::abs(Mismatch - BestMismatch) <= StopTieM;
        if (!Best || (Tie ? std::abs(Target.Offset) < std::abs(Best->Offset) : Mismatch < BestMismatch)) {
            Best = &Target;
            BestMismatch = Mismatch;
        }
    }
    Cycle.chooseStop(*Best);
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} stops {:.2f} -> phase {:.2f}, planted feet {:.2f} m off the cycle",
                                    Body.getFighterIndex() + 1, Cycle.getTime(), Best->TimeSec, BestMismatch));
    }
}

anim::Pose Fighter::getCyclePose(bool Crouched, float TimeSec) const {
    const ClipLibrary& Clips = Rules->Clips;
    anim::Pose Pose = anim::sampleClip(Clips.get(StanceName), 0.0f);
    if (!Crouched) {
        anim::layerPose(Pose, anim::sampleClip(Clips.get(clips::Walk), TimeSec));
        return Pose;
    }
    if (LegsMirrored) Pose = anim::joinLayers(Pose, anim::mirrorLegs(Pose));
    anim::layerPose(Pose, anim::sampleClip(getPlayed(Clips.get(clips::Crouch)), 0.0f));
    anim::layerPose(Pose, anim::sampleClip(getPlayed(Clips.get(clips::CrouchWalk)), TimeSec));
    return Pose;
}

void Fighter::updateRestLegs(bool LegAction, float Dt) {
    const anim::Clip& WalkClip = Rules->Clips.get(clips::Walk);
    const LegSource Source = Walk.isEngaged() && !Settle ? LegSource::Walk : LegSource::Stance;
    anim::Pose Legs;
    anim::Pose LegsStill;
    // Walking, stopping or held by the opponent in mid-step: the feet of the
    // step are placed along the floor (held, the phase stands, so do they).
    const LegCycle::Mode Mode = Walk.getMode();
    const bool Stepping = Source == LegSource::Walk &&
                          (Mode == LegCycle::Mode::Walking || Mode == LegCycle::Mode::Stopping ||
                           Mode == LegCycle::Mode::Held);
    if (!Stepping) Anchor.reset();
    if (Stepping && !Walk.getSteps().empty()) {
        // The feet go along the floor with the pelvis travel.
        anchorStep();
        const float Facing = Body.isFacingRight() ? 1.0f : -1.0f;
        const float PelvisX = WalkOdometer;
        Legs = placeStepFeet(anim::sampleClip(WalkClip, Walk.getTime()), Walk.getTime(),
                             PelvisX + Stride.Travel * Facing);
        LegsStill = placeStepFeet(anim::sampleClip(WalkClip, Walk.getStepFromTime()), Walk.getStepFromTime(), PelvisX);
    } else if (Source == LegSource::Walk) {
        Legs = anim::sampleClip(WalkClip, Walk.getTime());
        LegsStill = anim::sampleClip(WalkClip, Walk.getStepFromTime());
    } else {
        // The stance, with the feet where a settle has them.
        if (Settle) placeSettleFeet();
        Legs = applyLanding(getStanceLegs(RestFront));
        LegsStill = Legs;
    }
    if (LegAction) {
        // The action has the legs; these are the legs it leaves.
        LegFade.cancel();
    } else if (Source != ShownSource) {
        LegFadeByTravel = false;
        const auto getKind = [](LegSource Legs) { return Legs == LegSource::Walk ? PoseKind::Walk : PoseKind::Stance; };
        beginBlend(LegFade, ShownLegs, LegBlend, getKind(ShownSource), getKind(Source));
    }
    ShownSource = Source;
    const bool Fading = LegFade.isActive();
    ShownLegs = LegFade.step(Legs, LegFadeByTravel ? std::abs(Stride.Travel) : Dt);
    if (!LegFade.isActive()) LegFadeByTravel = false;
    ShownLegsStill = LegFade.peek(LegsStill);
    if (Fading && Stepping) {
        // Into a walk step the fade shapes the legs only: the feet are where
        // the step has them (a fade of the angles would drag the swing foot
        // along the floor).
        const auto keepFeet = [&](anim::Pose& Faded, const anim::Pose& Wanted) {
            const rig::LegStance Feet = Body.measureLegs(Wanted.Angles);
            const float PelvisHeight = Body.measureLegs(Faded.Angles).PelvisHeight;
            for (const BodyPart Foot : {BodyPart::FootL, BodyPart::FootR}) {
                const rig::FootPlacement& Place = Feet.getFoot(Foot);
                Body.reachFoot(Faded.Angles, Foot, PelvisHeight, Place.Ankle, Place.Angle);
            }
        };
        keepFeet(ShownLegs, Legs);
        keepFeet(ShownLegsStill, LegsStill);
    }
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
    Step = LegStep::plan(Body.measureLegsNow(), Body.measureLegs(Target.Angles), ClipLeft, ClipRight, Strike, Sec,
                         Tuning);
    LegsStepped = Step.isActive();
    if (LegsStepped) Body.dropLiftedFootOffsets();
    if constexpr (FIGHTER_DEBUG) {
        if (Step.isActive()) {
            debug::logEvent(std::format("P{} steps into {} in {:.2f} s: {}", Body.getFighterIndex() + 1, Top.Name, Sec,
                                        Step.describe()));
        }
    }
}

const anim::Clip& Fighter::getPlayed(const anim::Clip& Authored, bool OtherHand) const {
    const anim::Clip& Legs =
        LegsMirrored && anim::usesLegs(Authored) ? Rules->Clips.getMirrored(Authored) : Authored;
    return OtherHand ? Rules->Clips.getOtherHand(Legs) : Legs;
}

void Fighter::chooseStrikeWeapon(const anim::Clip& Authored) {
    // A weapon its strikers hold as authored; else one the strikers of the
    // other arm hold (the clip plays with the arms swapped); else the first.
    for (size_t Index = 0; Index < Weapons.size(); ++Index) {
        if (Authored.isStriker(Weapons[Index].Part)) {
            StrikeWeapon = Index;
            return;
        }
    }
    for (size_t Index = 0; Index < Weapons.size(); ++Index) {
        if (Authored.isStriker(anim::getMirroredArmPart(Weapons[Index].Part))) {
            StrikeWeapon = Index;
            StrikeOtherHand = true;
            return;
        }
    }
    if (!Weapons.empty()) StrikeWeapon = 0;
}

bool Fighter::shouldMirrorLegs() const {
    return Rules->Tuning.LegStep.Stance == StanceAfterStop::Mirror &&
           Body.measureLegsNow().getFrontFoot() == BodyPart::FootR;
}

anim::Pose Fighter::getStanceLegs(BodyPart FrontFoot) const {
    const anim::Pose Legs = anim::selectJoints(anim::sampleClip(Rules->Clips.get(StanceName), 0.0f),
                                               anim::getLegJoints());
    return FrontFoot == BodyPart::FootR ? anim::mirrorLegs(Legs) : Legs;
}

std::string Fighter::describeLegs() const {
    if (PendingAttack) {
        return std::format("standing up {:.2f} s -> {}", StandUpLeftSec, PendingAttack->Id);
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
    if (Settle) {
        return std::format("settling into the stance around {} (front {})", getBodyPartName(Settle->Stays),
                           getBodyPartName(RestFront));
    }
    if (Walk.isEngaged()) {
        return std::format("resting at phase {:.2f} (front {})", Walk.getTime(), getBodyPartName(Walk.getFrontFoot()));
    }
    return std::format("resting in the stance (front {})", getBodyPartName(RestFront));
}

std::string Fighter::describeStride() const {
    std::string Text;
    if (Settle) {
        Text = std::format("settle around {}{}, pelvis {:.3f} m to go", getBodyPartName(Settle->Stays),
                           RestLanding && RestLanding->SetDown ? " (set down)" : "", Settle->Left);
    } else if (Walk.getMode() == LegCycle::Mode::Walking) {
        // A full step of the cycle against what this one has made so far.
        float Planned = StepLength;
        if (const std::optional<size_t> Current = Walk.findStep(Walk.getTime())) {
            const CycleStep& Each = Walk.getSteps()[*Current];
            Planned = (Each.EndSec - Each.BeginSec) * Body.getStrideSpeed();
        }
        Text = std::format("step planned {:.2f} m, made {:.2f} m", Planned, StepTravel);
    } else {
        Text = std::format("rest{}, last settle {:.3f} m", RestLanding ? " off the stance" : "", SettleDone);
    }
    if (RestStep.isActive()) Text += std::format("; {}", RestStep.describe());
    if (Resteps > 0) Text += std::format("; re-steps {}, last {}", Resteps, LastRestep);
    return Text;
}

std::string Fighter::describeUpper() const {
    const anim::Clip* Top = getTopClip();
    // Where the wrist comes from: the clip on top, the stance, or the
    // weapon's own default (getStancePose()).
    std::string Wrist;
    if (Body.getDefaultWristAngle()) {
        const auto setsWrist = [](const anim::Clip& Source) {
            return !Source.Keys.empty() && Source.Keys.front().Target.HasWeapon;
        };
        const anim::Clip& Stance = Rules->Clips.get(StanceName);
        Wrist = std::format(", wrist from {}", Top && setsWrist(*Top) ? Top->Name
                                               : setsWrist(Stance)    ? StanceName
                                                                      : std::string("the weapon's default"));
    }
    if (!Top || !hasUpperJoints(*Top)) return StanceName + Wrist;
    const float Rate = State == FighterState::Attacking ? AttackRate : 1.0f;
    return anim::describePlayback(*Top, getTopClipTime(), Rate, Fade) + Wrist;
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
    for (const std::string& Name : Block.Clips) {
        if (!Name.empty() && &Source == &Clips.get(Name)) return PoseKind::Block;
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
        case FighterState::Blocking: return &getPlayed(getBlockClip());
        case FighterState::Reacting: return Rules->Clips.findReaction(Reaction);
        default: return nullptr;
    }
}

float Fighter::getTopClipTime() const {
    if (State != FighterState::Attacking) return StateSec;
    if (Contact != ContactStage::Recovering) return AttackTime;
    // Recovering from a contact: back from it to the start over the clip's
    // recovery.
    const float Recovery = AttackClip->DurationSec - AttackClip->ActiveEndSec;
    const float Share =
        Recovery > 0.0f ? std::clamp((AttackTime - AttackClip->ActiveEndSec) / Recovery, 0.0f, 1.0f) : 1.0f;
    return ContactClipSec * (1.0f - Share);
}

void Fighter::followPelvisTrack() {
    if (!Lunge.Moving) return;
    const float Target = anim::samplePelvisOffset(*AttackClip, AttackTime);
    Lunge.Planned = Target - Lunge.Followed;
    Lunge.Followed = Target;
    if (AttackTime >= AttackClip->PelvisTrack.back().TimeSec) Lunge.Moving = false;
}

void Fighter::checkPelvisTrack() {
    if (Lunge.Planned == 0.0f || Body.getPosture() != rig::Posture::Standing) return;
    const rig::PelvisController& Motion = Body.getController();
    const float Made = Motion.getClipTravelMade() * Lunge.Facing;
    Lunge.Made += Made;
    // Held back: the rest of the track is lost, the body does not catch up
    // later (nor go back by the keys that return it).
    if (std::abs(Lunge.Planned - Made) <= HeldTrackM || !Lunge.Stopped.empty()) return;
    stopPelvisTrack(Motion.getWallShift() != 0.0f ? "held back by a wall" : "held back by the opponent");
}

void Fighter::stopPelvisTrack(std::string_view Why) {
    Lunge.Moving = false;
    Lunge.Stopped = Why;
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} {} pelvis track {}: made {:+.2f} of {:+.2f} m so far",
                                    Body.getFighterIndex() + 1, Lunge.ClipName, Why, Lunge.Made, Lunge.Followed));
    }
}

bool Fighter::planLungeStep(const rig::LegStance& Now, const rig::LegStance& Wanted) {
    if (Lunge.Planned == 0.0f) return false;
    const LegStepTuning& Tuning = Rules->Tuning.LegStep;
    // Along the track's travel in the stance's frame (forward is +X): the
    // foot ahead leads, the other one trails.
    const float Heading = Lunge.Planned > 0.0f ? 1.0f : -1.0f;
    const bool LeftLeads = (Now.Left.Ankle.X - Now.Right.Ankle.X) * Heading > 0.0f;
    const BodyPart Leading = LeftLeads ? BodyPart::FootL : BodyPart::FootR;
    const BodyPart Trailing = LeftLeads ? BodyPart::FootR : BodyPart::FootL;
    const float Behind = (Wanted.getFoot(Leading).Ankle.X - Now.getFoot(Leading).Ankle.X) * Heading;
    if (!Now.getFoot(Leading).Planted || Behind <= Tuning.LungeStepDistance) return false;
    // Not towards an opponent this close: the foot could come down in its
    // legs (the rig holds it where it stands meanwhile).
    if (Heading > 0.0f && OpponentGap < Tuning.RestepClearance) return false;
    rig::LegStance Target = Wanted;
    Target.getFoot(Trailing) = Now.getFoot(Trailing);
    RestStep = LegStep::plan(Now, Target, false, false, false, Tuning.RestSec, Tuning);
    RestStepFresh = RestStep.isActive();
    if (!RestStepFresh) return false;
    Body.dropLiftedFootOffsets();
    ++Resteps;
    LastRestep = std::format("{} {:.2f} m (lunge)", getBodyPartName(Leading), Behind);
    if constexpr (FIGHTER_DEBUG) {
        debug::logEvent(std::format("P{} steps {} with the lunge: {:.2f} m behind the pelvis",
                                    Body.getFighterIndex() + 1, getBodyPartName(Leading), Behind));
    }
    return true;
}

std::string Fighter::describePelvisTrack() const {
    if (Lunge.ClipName.empty()) return "-";
    const anim::Clip* Clip = getMove() ? AttackClip : nullptr;
    std::string Text = std::format("{}: track {:+.2f} m", Lunge.ClipName, Lunge.Followed);
    if (Clip && Lunge.ClipName == Clip->Name) {
        Text += std::format(" (ends {:+.2f})", Clip->PelvisTrack.back().OffsetX);
    }
    Text += std::format(", made {:+.2f} m", Lunge.Made);
    if (Lunge.Moving) Text += ", moving";
    if (!Lunge.Stopped.empty()) Text += std::format(", {}: the rest is lost", Lunge.Stopped);
    return Text;
}

void Fighter::drawPelvisTrack() const {
    if constexpr (FIGHTER_DEBUG) {
        if (Lunge.ClipName.empty() || !getMove()) return;
        // From where the pelvis began the move: the track's offset so far
        // and, below it, how far it really took the pelvis.
        const Vec2 Pelvis = Body.getPartPosition(BodyPart::Pelvis);
        const Vec2 Start{Lunge.StartX, Pelvis.Y + PelvisTrackDrawLiftM};
        debug::drawArrow(debug::Cat::Forces, Start, {Lunge.Followed * Lunge.Facing, 0.0f}, "track");
        debug::drawArrow(debug::Cat::Forces, Start - Vec2{0.0f, PelvisTrackDrawGapM},
                         {Lunge.Made * Lunge.Facing, 0.0f}, Lunge.Stopped.empty() ? "made" : "made, stopped");
    }
}

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

/// Does the clip pose a joint of the upper body (not only the legs)?
bool hasUpperJoints(const anim::Clip& Source) {
    return (Source.Keys.front().Target.Mask & ~anim::getLegJoints()).any();
}

std::string_view getBlockZoneName(BlockZone Zone) {
    switch (Zone) {
        case BlockZone::High: return "High";
        case BlockZone::Mid: return "Mid";
        case BlockZone::Low: return "Low";
    }
    return "?";
}

std::string joinNames(const std::vector<std::string>& Names, std::string_view Separator) {
    std::string Text;
    for (const std::string& Name : Names) {
        if (!Text.empty()) Text += Separator;
        Text += Name;
    }
    return Text;
}

std::optional<float> getWristWish(const anim::Pose& Shown) {
    return Shown.HasWeapon ? std::optional(Shown.WeaponAngle) : std::nullopt;
}

} // namespace

} // namespace fighter::combat
