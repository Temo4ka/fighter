#include "combat/battle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "combat/fighter.hpp"
#include "combat/tuning.hpp"
#include "debug/draw.hpp"
#include "physics/world.hpp"
#include "rig/pelvis_controller.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"

namespace fighter::combat {
namespace {

// Per-fighter constants, in the order of Battle::Simulation::Fighters (left, right).
constexpr std::array<const char*, 2> PlayerNames = {"P1", "P2"};
constexpr std::array<float, 2> SpawnSide = {-1.0f, 1.0f};

/// Fighters spawn this far above the floor so that the feet do not start
/// inside it, m.
constexpr float SpawnLift = 0.005f;
/// Arena geometry, m.
constexpr float FloorDepth = 1.0f;
constexpr float WallHeight = 4.0f;
constexpr float WallThickness = 0.2f;

/// Hits stay on the debug screen this long, s.
constexpr float HitDisplaySec = 0.5f;
constexpr float HitArrowScale = 0.04f;   // m per N*s
/// Radius of the circle drawn around a striking part, m.
constexpr float HitboxRadius = 0.16f;

void addArena(physics::World& PhysWorld, const ArenaConfig& Arena);
rig::RigSetup makeRigSetup(const stats::PhysicalProfile& Profile, float StartX, uint8_t Index);
void keepApart(Fighter& Left, Fighter& Right, float ArenaHalfWidth, float BodyHalfWidth);
std::string getPhysicalParts(const rig::Rig& Body);

} // namespace

/// Everything that lives only inside a running battle.
struct Battle::Simulation {
    /// A hit kept for a short while so that it can be seen.
    struct RecentHit {
        physics::HitEvent Hit;
        Vec2 Direction;        ///< From the attacking part towards the victim's part.
        float AgeSec = 0.0f;
    };

    physics::World PhysWorld;
    ClipSet Clips;
    float BodyHalfWidth = 0.0f;      ///< CombatTuning::BodyHalfWidth.
    std::vector<Fighter> Fighters;   ///< [0] left, [1] right; never resized after creation.
    std::vector<RecentHit> RecentHits;
};

Battle::Battle(const BattleConfig& Config) : Cfg(Config) {
    const stats::BalanceTable Balance = stats::BalanceTable::getDefaults();
    const CombatTuning Tuning = loadCombatTuning(Cfg.DataDir / "combat.json");

    Sim = std::make_unique<Simulation>(Simulation{
        .PhysWorld = physics::World({.Gravity = Cfg.Arena.Gravity, .HitSpeedThreshold = Tuning.HitSpeedThreshold}),
        .Clips = ClipSet::load(Cfg.DataDir / "poses"),
        .BodyHalfWidth = Tuning.BodyHalfWidth,
    });
    addArena(Sim->PhysWorld, Cfg.Arena);

    const std::array<const FighterConfig*, 2> Configs = {&Cfg.Left, &Cfg.Right};
    Sim->Fighters.reserve(Configs.size());
    for (auto&& [Index, FighterCfg, Side] : std::views::zip(std::views::iota(uint8_t{0}), Configs, SpawnSide)) {
        const rig::RigDef Body = rig::loadRigDef(Cfg.DataDir / "rigs" / (FighterCfg->RigId + ".json"));
        const stats::PhysicalProfile Profile = stats::computeProfile(FighterCfg->Stats, FighterCfg->Loadout, Balance);
        const float StartX = Side * Tuning.SpawnDistance * 0.5f;
        Sim->Fighters.emplace_back(Sim->PhysWorld, Body, Sim->Clips, Profile, makeRigSetup(Profile, StartX, Index));
    }
    publishSnapshot();
}

Battle::~Battle() = default;
Battle::Battle(Battle&& Other) noexcept = default;
Battle& Battle::operator=(Battle&& Other) noexcept = default;

void Battle::update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt) {
    if (Result) return;
    const float StepDt = static_cast<float>(Dt);

    // Explicit order: controllers plan -> spacing -> bodies move -> physics -> hits.
    Fighter& Left = Sim->Fighters[0];
    Fighter& Right = Sim->Fighters[1];
    Left.control(LeftCmd, StepDt);
    Right.control(RightCmd, StepDt);
    keepApart(Left, Right, Cfg.Arena.HalfWidthM, Sim->BodyHalfWidth);
    Left.applyControl(StepDt);
    Right.applyControl(StepDt);
    Sim->PhysWorld.step(StepDt);

    for (auto& Recent : Sim->RecentHits) Recent.AgeSec += StepDt;
    std::erase_if(Sim->RecentHits, [](const auto& Recent) { return Recent.AgeSec > HitDisplaySec; });

    // Physics guesses the attacker from the velocities; the attack state
    // decides. Contacts without a striking limb (feet bumping while walking,
    // a chest pushing) are not hits. An attack lands once: the strongest of
    // its contacts in the step it first touches the opponent.
    std::array<std::optional<physics::HitEvent>, 2> Strikes;
    for (const auto& Contact : Sim->PhysWorld.getHitEvents()) {
        physics::HitEvent Hit = Contact;
        const auto IsStrike = [&] { return Sim->Fighters[Hit.Attacker.Fighter].isStrikingWith(Hit.Attacker.Part); };
        if (!IsStrike()) std::swap(Hit.Attacker, Hit.Victim);
        if (!IsStrike()) continue;
        std::optional<physics::HitEvent>& Strongest = Strikes[Hit.Attacker.Fighter];
        if (!Strongest || Hit.Impulse > Strongest->Impulse) Strongest = Hit;
    }

    Hits.clear();
    for (const auto& Strike : Strikes) {
        if (!Strike) continue;
        const physics::HitEvent& Hit = *Strike;
        Fighter& Attacker = Sim->Fighters[Hit.Attacker.Fighter];
        Fighter& Victim = Sim->Fighters[Hit.Victim.Fighter];
        Attacker.onStrikeLanded();
        // A hit pushes the victim away from the attacker.
        const float AttackerX = Attacker.getRig().getPartPosition(BodyPart::Pelvis).X;
        const float VictimX = Victim.getRig().getPartPosition(BodyPart::Pelvis).X;
        Victim.onHit(Hit, VictimX >= AttackerX ? 1.0f : -1.0f);
        Hits.push_back(Hit);

        const Vec2 Direction = (Victim.getRig().getPartPosition(Hit.Victim.Part) -
                                Attacker.getRig().getPartPosition(Hit.Attacker.Part)).getNormalized();
        Sim->RecentHits.push_back({.Hit = Hit, .Direction = Direction});
        debug::logEvent(std::format("{} {} -> {} {}: J={:.1f} N*s, v={:.1f} m/s",
                                    PlayerNames[Hit.Attacker.Fighter], getBodyPartName(Hit.Attacker.Part),
                                    PlayerNames[Hit.Victim.Fighter], getBodyPartName(Hit.Victim.Part),
                                    Hit.Impulse, Hit.ApproachSpeed));
    }

    ElapsedSec += Dt;
    ++Tick;

    if (ElapsedSec >= Cfg.RoundTimeSec) {
        const float HpLeft = Sim->Fighters[0].getHp();
        const float HpRight = Sim->Fighters[1].getHp();
        finish(HpLeft > HpRight ? Winner::Left : HpRight > HpLeft ? Winner::Right : Winner::Draw);
    }

    publishSnapshot();
    drawDebug();
}

void Battle::finish(Winner Outcome) {
    BattleResult Round;
    Round.WinnerSide = Outcome;
    Round.TimeSec = ElapsedSec;
    Round.Left.HpLeft = Sim->Fighters[0].getHp();
    Round.Right.HpLeft = Sim->Fighters[1].getHp();
    Result = Round;
}

void Battle::publishSnapshot() {
    Snapshot.Tick = Tick;
    Snapshot.TimeLeftSec = std::max(0.0, Cfg.RoundTimeSec - ElapsedSec);
    Snapshot.Arena.HalfWidthM = Cfg.Arena.HalfWidthM;
    for (auto&& [Player, View] : std::views::zip(Sim->Fighters, Snapshot.Fighters)) Player.fillView(View);
}

void Battle::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) {
        Sim->PhysWorld.drawDebug();

        for (auto&& [Player, Name] : std::views::zip(Sim->Fighters, PlayerNames)) {
            const rig::Rig& Body = Player.getRig();
            Body.drawDebug();
            for (size_t Index = 0; Index < BodyPartCount; ++Index) {
                const auto Part = static_cast<BodyPart>(Index);
                if (Player.isStrikingWith(Part)) {
                    debug::drawCircle(debug::Cat::Hitbox, Body.getPartPosition(Part), HitboxRadius);
                }
            }

            const Vec2 Feet = Body.getFloorPoint();
            debug::setPanel(std::format("{} pos", Name), std::format("({:+.2f}, {:+.2f}) m", Feet.X, Feet.Y));
            std::string State(rig::getPostureName(Body.getPosture()));
            if (Body.getPosture() != rig::Posture::Standing) State += std::format(" {:.2f} s", Body.getPostureSec());
            debug::setPanel(std::format("{} state", Name), State);
            const rig::PelvisController& Controller = Body.getController();
            debug::setPanel(std::format("{} pelvis", Name),
                            std::format("x {:+.2f} m, v {:+.2f} m/s (walk {:+.2f}, knockback {:+.2f})",
                                        Controller.getX(), Controller.getVelocity(), Controller.getWalkVelocity(),
                                        Controller.getKnockback()));
            debug::setPanel(std::format("{} physical", Name), getPhysicalParts(Body));
            debug::setPanel(std::format("{} clip", Name),
                            std::format("{} {:.2f} s{}", Player.getClipName(), Player.getClipTime(),
                                        Player.isAttackActive() ? "  ACTIVE" : ""));
            debug::setPanel(std::format("{} stiffness", Name), std::format("{:.2f}", Body.getStiffness()));
            debug::setPanel(std::format("{} body", Name),
                            std::format("{:.1f} kg, motors {:.0f} Nm max, gain {:.1f}/s, walk {:.2f} m/s",
                                        Body.getTotalMass(), Body.getMotorMaxTorque(), Body.getMotorGain(),
                                        Body.getWalkSpeed()));
            debug::setPanel(std::format("{} torque", Name),
                            std::format("{:.0f} Nm total", Body.getMotorTorqueSum()));
            debug::setPanel(std::format("{} hp", Name),
                            std::format("{:.0f} / {:.0f}", Player.getHp(), Player.getProfile().MaxHp));
        }

        for (const auto& Recent : Sim->RecentHits) {
            debug::ScopedSide Owner(Recent.Hit.Victim.Fighter == 0 ? debug::Side::Left : debug::Side::Right);
            const Vec2 Arrow = Recent.Direction * Recent.Hit.Impulse * HitArrowScale;
            debug::drawArrow(debug::Cat::Forces, Recent.Hit.Point, Arrow, std::format("J={:.1f}", Recent.Hit.Impulse));
        }
        debug::setPanel("round", std::format("{:.1f} s left", Snapshot.TimeLeftSec));
    }
}

namespace {

void addArena(physics::World& PhysWorld, const ArenaConfig& Arena) {
    const float HalfWidth = Arena.HalfWidthM;
    const physics::Body Ground = PhysWorld.createBody({.Type = physics::BodyType::Static});

    // Floor: its top is y = 0.
    PhysWorld.addShape(Ground, {
        .Kind = physics::ShapeKind::Box,
        .Center = {0.0f, -FloorDepth * 0.5f},
        .HalfExtents = {HalfWidth + WallThickness, FloorDepth * 0.5f},
        .Friction = 0.9f,
    });
    // Walls: their inner faces are x = +-HalfWidth.
    for (const auto& Side : {-1.0f, 1.0f}) {
        PhysWorld.addShape(Ground, {
            .Kind = physics::ShapeKind::Box,
            .Center = {Side * (HalfWidth + WallThickness * 0.5f), WallHeight * 0.5f},
            .HalfExtents = {WallThickness * 0.5f, WallHeight * 0.5f},
        });
    }
}

rig::RigSetup makeRigSetup(const stats::PhysicalProfile& Profile, float StartX, uint8_t Index) {
    rig::RigSetup Setup;
    Setup.Origin = {StartX, SpawnLift};
    Setup.FacingRight = StartX < 0.0f;   // fighters face each other
    Setup.FighterIndex = Index;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    Setup.MoveSpeedScale = Profile.MoveSpeedScale;
    return Setup;
}

/// Keeps the planned pelvis positions inside the arena and the fighters at
/// least a body width apart. Kinematic pelvises do not collide, so this is
/// their "collision": the overlap is split so that the heavier fighter gives
/// way less; a fighter against a wall cannot give way at all.
void keepApart(Fighter& Left, Fighter& Right, float ArenaHalfWidth, float BodyHalfWidth) {
    const float MaxX = ArenaHalfWidth - BodyHalfWidth;
    rig::Rig& LeftBody = Left.getRig();
    rig::Rig& RightBody = Right.getRig();
    const bool LeftUp = LeftBody.getPosture() != rig::Posture::KnockedDown;
    const bool RightUp = RightBody.getPosture() != rig::Posture::KnockedDown;
    rig::PelvisController& LeftMotion = LeftBody.getController();
    rig::PelvisController& RightMotion = RightBody.getController();
    if (LeftUp) LeftMotion.limit(-MaxX, MaxX);
    if (RightUp) RightMotion.limit(-MaxX, MaxX);
    // A ragdoll on the floor is pushed by the legs of the other fighter instead.
    if (!LeftUp || !RightUp) return;

    const float MinGap = 2.0f * BodyHalfWidth;
    const float Overlap = MinGap - (RightMotion.getPlannedX() - LeftMotion.getPlannedX());
    if (Overlap <= 0.0f) return;
    const float LeftMass = LeftBody.getTotalMass();
    const float RightMass = RightBody.getTotalMass();
    LeftMotion.shift(-Overlap * RightMass / (LeftMass + RightMass));
    RightMotion.shift(Overlap * LeftMass / (LeftMass + RightMass));
    LeftMotion.limit(-MaxX, MaxX);
    RightMotion.limit(-MaxX, MaxX);

    const float Rest = MinGap - (RightMotion.getPlannedX() - LeftMotion.getPlannedX());
    if (Rest <= 0.0f) return;
    if (LeftMotion.getPlannedX() <= -MaxX) {
        RightMotion.shift(Rest);
    } else {
        LeftMotion.shift(-Rest);
    }
}

/// Names of the parts that are physical right now, for the debug panel.
std::string getPhysicalParts(const rig::Rig& Body) {
    std::string Names;
    size_t Count = 0;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        if (Body.isKinematic(Part)) continue;
        if (!Names.empty()) Names += ' ';
        Names += getBodyPartName(Part);
        ++Count;
    }
    return Count == BodyPartCount ? "all (ragdoll)" : Names;
}

} // namespace

} // namespace fighter::combat
