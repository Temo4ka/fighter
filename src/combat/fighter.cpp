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
}

void Fighter::control(const PlayerCommands& Cmd, float Dt) {
    if (Attack) {
        AttackTime += Dt;
        if (Attack->isFinishedAt(AttackTime)) Attack = nullptr;
    }
    // Holding the button repeats the attack.
    if (!Attack && Cmd.Punch) startAttack(Clips->Jab);
    if (!Attack && Cmd.Kick) startAttack(Clips->Kick);

    const rig::ControlParams& Control = Body.getControl();
    const bool CanMove = !Attack || Attack->AllowMove;
    const float MoveX = CanMove ? std::clamp(Cmd.MoveX, -1.0f, 1.0f) : 0.0f;
    const bool WantsToMove = std::abs(MoveX) > MoveDeadZone;
    const float Period = Clips->Walk.DurationSec;

    float Velocity = 0.0f;
    if (!WantsToMove) WalkHeldSec = 0.0f;
    if (WantsToMove) {
        const bool Forward = (MoveX > 0.0f) == Body.isFacingRight();
        Velocity = MoveX * Control.WalkSpeed * (Forward ? 1.0f : Control.BackwardSpeedScale);
        // The cycle follows the distance actually covered, so the legs keep
        // up with the body and do not march on the spot when the way is
        // blocked. The minimum rate lets the first step start from standing.
        const float Speed = std::abs(Body.getCenterOfMassVelocity().X);
        const float MinRate = WalkHeldSec < Control.WalkStartSec ? Control.WalkCycleMinRate : 0.0f;
        const float Rate = std::max(Speed / Control.WalkSpeed, MinRate);
        WalkHeldSec += Dt;
        WalkDirection = Forward ? 1.0f : -1.0f;
        WalkTime = std::fmod(WalkTime + Dt * Rate * WalkDirection + Period, Period);
        Walking = true;
    } else if (Walking) {
        // Finish the step: friction pins the feet, so the stance only comes
        // back if the cycle plays on to phase 0, where the legs are placed
        // as in the stance.
        const float Next = WalkTime + Dt * WalkDirection;
        Walking = Next > 0.0f && Next < Period;
        WalkTime = Walking ? Next : 0.0f;
    }

    anim::Pose Target = anim::sampleClip(Clips->Stance, 0.0f);
    if (Walking) anim::layerPose(Target, anim::sampleClip(Clips->Walk, WalkTime));
    if (Attack) anim::layerPose(Target, anim::sampleClip(*Attack, AttackTime));

    Body.setTargetAngles(Target.Angles);
    Body.setMoveVelocity(Velocity);
    Body.setBaseStiffness(Attack ? Attack->Stiffness : 1.0f);
    Body.applyControl(Dt);
}

void Fighter::onHit(const physics::HitEvent& Hit) { Body.applyHit(Hit.Impulse); }

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
