#include "combat/fighter.hpp"

#include <algorithm>
#include <cmath>

namespace fighter::combat {
ClipSet ClipSet::load(const std::filesystem::path& PosesDir) {
    return {
        .Stance = anim::loadClip(PosesDir / "stance.json"),
        .Walk = anim::loadClip(PosesDir / "walk.json"),
        .Jab = anim::loadClip(PosesDir / "jab.json"),
        .Kick = anim::loadClip(PosesDir / "kick.json"),
    };
}

Fighter::Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const ClipSet& NewClips,
                 const stats::PhysicalProfile& NewProfile, const rig::RigSetup& Setup, std::optional<float> StartHp)
    : Body(PhysWorld, Description, Setup), Clips(&NewClips), Profile(NewProfile),
      Hp(std::clamp(StartHp.value_or(NewProfile.MaxHp), 0.0f, NewProfile.MaxHp)) {
    Body.setTargetAngles(anim::sampleClip(NewClips.Stance, 0.0f).Angles);
    Body.snapToTargets();
}

bool Fighter::control(const PlayerCommands& Cmd, float Dt) {
    // Knocked down or getting up: no attacks, no walking, the stance waits.
    const bool Standing = Body.getPosture() == rig::Posture::Standing;
    if (Attack) {
        AttackTime += Dt;
        if (!Standing || Attack->isFinishedAt(AttackTime)) Attack = nullptr;
    }
    // Holding the button repeats the attack. PLACEHOLDER until 2.2/2.3: the
    // heavy punch and the low kick have no clips yet, blocking and crouching
    // are ignored.
    // PLACEHOLDER move ids until the moves are read from data/moves/ (2.3).
    const bool CanAttack = Standing && !Attack;
    if (CanAttack && Cmd.Jab) startAttack(Clips->Jab, "jab");
    if (CanAttack && !Attack && Cmd.BodyKick) startAttack(Clips->Kick, "body_kick");
    const bool Started = CanAttack && Attack;

    const rig::ControlParams& Control = Body.getControl();
    const bool CanMove = Standing && (!Attack || Attack->AllowMove);
    const float MoveX = CanMove ? std::clamp(Cmd.MoveX, -1.0f, 1.0f) : 0.0f;
    const bool WantsToMove = std::abs(MoveX) > MoveDeadZone;
    const float Period = Clips->Walk.DurationSec;

    float Velocity = 0.0f;
    if (WantsToMove) {
        const bool Forward = (MoveX > 0.0f) == Body.isFacingRight();
        Velocity = MoveX * Body.getWalkSpeed() * (Forward ? 1.0f : Control.BackwardSpeedScale);
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

    anim::Pose Target = anim::sampleClip(Clips->Stance, 0.0f);
    if (Walking) anim::layerPose(Target, anim::sampleClip(Clips->Walk, WalkTime));
    if (Attack) anim::layerPose(Target, anim::sampleClip(*Attack, AttackTime));

    Body.setTargetAngles(Target.Angles);
    Body.setMoveVelocity(Velocity);
    Body.setBaseStiffness(Attack ? Attack->Stiffness : 1.0f);
    Body.planMotion(Dt);
    return Started;
}

void Fighter::applyControl(float Dt) { Body.applyControl(Dt); }

void Fighter::onHit(const physics::HitEvent& Hit, float Direction) { Body.applyHit(Hit.Impulse, Direction); }

std::string_view Fighter::getClipName() const {
    if (Attack) return Attack->Name;
    return Walking ? Clips->Walk.Name : Clips->Stance.Name;
}

float Fighter::getClipTime() const {
    if (Attack) return AttackTime;
    return Walking ? WalkTime : 0.0f;
}

bool Fighter::isAttackActive() const { return Attack && Attack->isActiveAt(AttackTime); }

bool Fighter::isStrikingWith(BodyPart Part) const {
    return isAttackActive() && !AttackLanded && Attack->isStriker(Part);
}

void Fighter::fillView(FighterView& View) const {
    View.Position = Body.getFloorPoint();
    View.FacingRight = Body.isFacingRight();
    View.Hp = Hp;
    View.MaxHp = Profile.MaxHp;
    // PLACEHOLDER until the state machine (2.3): no stamina, crouch, block,
    // reactions or walls yet.
    View.Stamina = 0.0f;
    View.MaxStamina = 0.0f;
    View.MoveId = getMoveId();
    View.Phase = AttackPhase::None;
    if (Attack) {
        View.Phase = AttackTime < Attack->ActiveBeginSec ? AttackPhase::Startup
                     : Attack->isActiveAt(AttackTime)     ? AttackPhase::Active
                                                          : AttackPhase::Recovery;
    }
    switch (Body.getPosture()) {
        case rig::Posture::KnockedDown: View.State = FighterState::KnockedDown; break;
        case rig::Posture::GettingUp: View.State = FighterState::GettingUp; break;
        case rig::Posture::Standing:
            View.State = Attack ? FighterState::Attacking : Walking ? FighterState::Walking : FighterState::Idle;
            break;
    }
    View.Reaction = ReactionLevel::None;
    View.AgainstWall = false;
    Body.getPartTransforms(View.Parts);
}

void Fighter::startAttack(const anim::Clip& NewAttack, std::string_view NewMoveId) {
    Attack = &NewAttack;
    MoveId = NewMoveId;
    AttackTime = 0.0f;
    AttackLanded = false;
}

} // namespace fighter::combat
