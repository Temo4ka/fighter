#include "combat/fighter.hpp"

#include <algorithm>
#include <cmath>

namespace fighter::combat {
namespace {

/// Stick input below this does not count as walking.
constexpr float MoveDeadZone = 0.1f;

} // namespace

ClipSet ClipSet::load(const std::filesystem::path& PosesDir) {
    return {
        .Stance = anim::loadClip(PosesDir / "stance.json"),
        .Walk = anim::loadClip(PosesDir / "walk.json"),
        .Jab = anim::loadClip(PosesDir / "jab.json"),
        .Kick = anim::loadClip(PosesDir / "kick.json"),
    };
}

Fighter::Fighter(physics::World& PhysWorld, const rig::RigDef& Description, const ClipSet& NewClips,
                 const stats::PhysicalProfile& NewProfile, const rig::RigSetup& Setup)
    : Body(PhysWorld, Description, Setup), Clips(&NewClips), Profile(NewProfile), Hp(NewProfile.MaxHp) {
    Body.setTargetAngles(anim::sampleClip(NewClips.Stance, 0.0f).Angles);
    Body.snapToTargets();
}

void Fighter::control(const PlayerCommands& Cmd, float Dt) {
    // Knocked down or getting up: no attacks, no walking, the stance waits.
    const bool Standing = Body.getPosture() == rig::Posture::Standing;
    if (Attack) {
        AttackTime += Dt;
        if (!Standing || Attack->isFinishedAt(AttackTime)) Attack = nullptr;
    }
    // Holding the button repeats the attack.
    if (Standing && !Attack && Cmd.Punch) startAttack(Clips->Jab);
    if (Standing && !Attack && Cmd.Kick) startAttack(Clips->Kick);

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

bool Fighter::isStrikingWith(BodyPart Part) const { return isAttackActive() && Attack->isStriker(Part); }

void Fighter::fillView(FighterView& View) const {
    View.Position = Body.getFloorPoint();
    View.FacingRight = Body.isFacingRight();
    View.Hp = Hp;
    View.MaxHp = Profile.MaxHp;
    Body.getPartTransforms(View.Parts);
}

void Fighter::startAttack(const anim::Clip& NewAttack) {
    Attack = &NewAttack;
    AttackTime = 0.0f;
}

} // namespace fighter::combat
