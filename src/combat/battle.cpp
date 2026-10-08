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

#include "combat/clip_library.hpp"
#include "combat/fighter.hpp"
#include "combat/moves.hpp"
#include "combat/moveset.hpp"
#include "combat/reactions.hpp"
#include "combat/tuning.hpp"
#include "core/log.hpp"
#include "debug/draw.hpp"
#include "physics/world.hpp"
#include "rig/pelvis_controller.hpp"
#include "rig/rig.hpp"
#include "rig/rig_def.hpp"
#include "rig/contact.hpp"
#include "stats/loading.hpp"

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
// Only the debug build draws the panel.
[[maybe_unused]] std::string describeAction(const FighterView& View, const Fighter& Player);
rig::ContactParams getContactParams(const ArenaConfig& Arena, const CombatTuning& Tuning);
/// The weapons and shields of the items in both hands, on the forearms of
/// \p Mount (main hand: Part, the other: OffPart). A two-handed item is held
/// by the main hand.
std::vector<rig::HeldItem> getHeldItems(const stats::Loadout& Gear, const rig::WeaponMount& Mount);
rig::RigSetup makeRigSetup(const stats::PhysicalProfile& Profile, const stats::Loadout& Gear,
                           const rig::RigDef& Body, float StartX, uint8_t Index);

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
    BattleRules Rules;               ///< Moves, clips, tuning and reactions; the fighters point here.
    std::vector<Fighter> Fighters;   ///< [0] left, [1] right; never resized after creation.
    std::vector<RecentHit> RecentHits;
    /// Keeps the fighters from passing through each other: its stages run
    /// around the physics step (rig/contact.hpp).
    rig::ContactResolver Contacts;
};

Battle::Battle(const BattleConfig& Config) : Cfg(Config) {
    const stats::BalanceTable Balance = stats::loadBalanceTable(Cfg.DataDir / "balance.json");
    const CombatTuning Tuning = loadCombatTuning(Cfg.DataDir / "combat.json");

    MoveLibrary Moves = MoveLibrary::load(Cfg.DataDir);
    ClipLibrary Clips = ClipLibrary::load(Cfg.DataDir / "poses", Moves);

    Sim = std::make_unique<Simulation>(Simulation{
        .PhysWorld = physics::World({.Gravity = Cfg.Arena.Gravity,
                                     .StepPasses = Tuning.PhysicsSteps,
                                     .SubSteps = Tuning.PhysicsSubSteps,
                                     .ContactHertz = Tuning.ContactHertz,
                                     .FighterFriction = Tuning.FighterFriction,
                                     .HitSpeedThreshold = Tuning.HitSpeedThreshold}),
        .Rules = {.Moves = std::move(Moves),
                  .Clips = std::move(Clips),
                  .Tuning = Tuning,
                  .Reactions = loadReactionTable(Cfg.DataDir / "reactions.json"),
                  .PelvisLimitX = Cfg.Arena.HalfWidthM - Tuning.BodyHalfWidth},
        .Contacts = rig::ContactResolver(getContactParams(Cfg.Arena, Tuning)),
    });
    addArena(Sim->PhysWorld, Cfg.Arena);

    const std::array<const FighterConfig*, 2> Configs = {&Cfg.Left, &Cfg.Right};
    Sim->Fighters.reserve(Configs.size());
    for (auto&& [Index, FighterCfg, Side] : std::views::zip(std::views::iota(uint8_t{0}), Configs, SpawnSide)) {
        const rig::RigDef Body = rig::loadRigDef(Cfg.DataDir / "rigs" / (FighterCfg->RigId + ".json"));
        const stats::PhysicalProfile Profile = stats::computeProfile(FighterCfg->Stats, FighterCfg->Loadout, Balance);
        const float StartX = Side * Cfg.SpawnDistanceM.value_or(Tuning.SpawnDistance) * 0.5f;
        Sim->Fighters.emplace_back(Sim->PhysWorld, Body, Sim->Rules, Profile, FighterCfg->Loadout,
                                   makeRigSetup(Profile, FighterCfg->Loadout, Body, StartX, Index), FighterCfg->StartHp);
    }
    publishSnapshot();
}

Battle::~Battle() = default;
Battle::Battle(Battle&& Other) noexcept = default;
Battle& Battle::operator=(Battle&& Other) noexcept = default;

void Battle::update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt) {
    Events.clear();
    if (Result) {
        settle(static_cast<float>(Dt));
        return;
    }
    const float StepDt = static_cast<float>(Dt);

    // Explicit order: controllers plan -> contacts before the step (walls,
    // spacing) -> bodies move -> physics -> contacts after the step (posed
    // limbs) -> hits -> contacts of the hits (push-out).
    Fighter& Left = Sim->Fighters[0];
    Fighter& Right = Sim->Fighters[1];
    std::array<rig::Posture, 2> PostureBefore{};
    const std::array<const PlayerCommands*, 2> Commands = {&LeftCmd, &RightCmd};
    const std::array<Surroundings, 2> Around = {getSurroundings(0), getSurroundings(1)};
    for (auto&& [Index, Player, Cmd, Before, Near] :
         std::views::zip(std::views::iota(uint8_t{0}), Sim->Fighters, Commands, PostureBefore, Around)) {
        Before = Player.getRig().getPosture();
        const MoveDef* Started = Player.control(*Cmd, Near, StepDt);
        if (!Started) continue;
        ++Reports[Index].Moves[Started->Id].Thrown;
        Events.push_back(StrikeStarted{.Fighter = Index, .MoveId = Started->Id});
    }
    Sim->Contacts.beforeStep(Left.getRig(), Right.getRig(), StepDt);
    Left.applyControl(StepDt);
    Right.applyControl(StepDt);
    Sim->PhysWorld.step(StepDt);
    // Nothing in physics stops a posed limb: a kick that sank into the
    // opponent's posed legs or pelvis goes back to the contact. The hits of
    // the step are already collected, with the speed the limb came in at.
    stopPosedLimbs();
    for (auto& Recent : Sim->RecentHits) Recent.AgeSec += StepDt;
    std::erase_if(Sim->RecentHits, [](const auto& Recent) { return Recent.AgeSec > HitDisplaySec; });

    // Physics guesses the attacker from the velocities; the attack state
    // decides. Contacts without a striking limb (feet bumping while walking,
    // a chest pushing) are not hits. An attack lands once: the strongest of
    // its contacts in the step it first touches the opponent. The move is
    // kept with the contact: a trade interrupts the attacks it resolves.
    struct Strike {
        physics::HitEvent Hit;
        const MoveDef* Move = nullptr;
        bool Jammed = false;   ///< Ran into the opponent in its startup: not a clean strike.
    };
    std::array<std::optional<Strike>, 2> Strikes;
    for (const auto& Contact : Sim->PhysWorld.getHitEvents()) {
        physics::HitEvent Hit = Contact;
        const auto IsStrike = [&] { return Sim->Fighters[Hit.Attacker.Fighter].isStrikingWith(Hit.Attacker.Part); };
        if (!IsStrike()) std::swap(Hit.Attacker, Hit.Victim);
        if (!IsStrike()) continue;
        std::optional<Strike>& Strongest = Strikes[Hit.Attacker.Fighter];
        if (!Strongest || Hit.Impulse > Strongest->Hit.Impulse) {
            const Fighter& Striker = Sim->Fighters[Hit.Attacker.Fighter];
            Strongest = Strike{.Hit = Hit, .Move = Striker.getMove(), .Jammed = Striker.isJammed()};
        }
    }

    for (const auto& Landed : Strikes) {
        if (!Landed) continue;
        const physics::HitEvent& Hit = Landed->Hit;
        const MoveDef& Move = *Landed->Move;
        Fighter& Attacker = Sim->Fighters[Hit.Attacker.Fighter];
        Fighter& Victim = Sim->Fighters[Hit.Victim.Fighter];
        // A fighter on the floor is not hit (no juggling): the contact is a bump.
        if (!Victim.isHittable()) continue;
        // A hit pushes the victim away from the attacker.
        const float AttackerX = Attacker.getRig().getPartPosition(BodyPart::Pelvis).X;
        const float VictimX = Victim.getRig().getPartPosition(BodyPart::Pelvis).X;
        const HitOutcome Outcome = Victim.takeHit(Hit, Move, Attacker.getPowerScale(Move),
                                                  VictimX >= AttackerX ? 1.0f : -1.0f, Landed->Jammed);
        Attacker.onStrikeLanded(!Outcome.Blocked && !Landed->Jammed);
        Sim->Contacts.onStrikeLanded(Attacker.getRig(), Victim.getRig());

        FighterReport& Hitter = Reports[Hit.Attacker.Fighter];
        FighterReport& Target = Reports[Hit.Victim.Fighter];
        StrikeStats& Stats = Hitter.Moves[Move.Id];
        ++Stats.Landed;
        Stats.Blocked += Outcome.Blocked ? 1 : 0;
        Stats.Damage += Outcome.Damage;
        Hitter.DamageDealt += Outcome.Damage;
        Target.DamageTaken += Outcome.Damage;
        PartReport& Part = Target.HitsTaken[static_cast<size_t>(Hit.Victim.Part)];
        ++Part.Hits;
        Part.Damage += Outcome.Damage;
        Events.push_back(StrikeLanded{.Contact = Hit,
                                      .MoveId = Move.Id,
                                      .Strength = Outcome.Strength,
                                      .Damage = Outcome.Damage,
                                      .Reaction = Outcome.Reaction,
                                      .Blocked = Outcome.Blocked});

        const Vec2 Direction = (Victim.getRig().getPartPosition(Hit.Victim.Part) -
                                Attacker.getRig().getPartPosition(Hit.Attacker.Part)).getNormalized();
        Sim->RecentHits.push_back({.Hit = Hit, .Direction = Direction});
        debug::logEvent(std::format("{} {} -> {}: {:.2f} m/s -> {}, {:.1f} dmg{}{} (J={:.1f} N*s)",
                                    PlayerNames[Hit.Attacker.Fighter], Move.Id, getBodyPartName(Hit.Victim.Part),
                                    Outcome.Strength, getReactionLevelName(Outcome.Reaction), Outcome.Damage,
                                    Outcome.Blocked ? ", blocked" : "", Landed->Jammed ? ", jammed" : "",
                                    Hit.Impulse));
    }

    for (auto&& [Index, Player] : std::views::zip(std::views::iota(uint8_t{0}), Sim->Fighters)) {
        if (!Player.takeExhaustedNotice()) continue;
        Events.push_back(Exhausted{.Fighter = Index});
        debug::logEvent(std::format("{} is exhausted", PlayerNames[Index]));
    }

    // The posture changes in the hits (a knockdown or knockout) and in the
    // rig's own timers (getting up). A knockout fall is told too (dust), but
    // counted as a knockout, not a knockdown.
    for (auto&& [Index, Player, Before] : std::views::zip(std::views::iota(uint8_t{0}), Sim->Fighters, PostureBefore)) {
        const rig::Posture After = Player.getRig().getPosture();
        if (After == Before) continue;
        if (After == rig::Posture::KnockedDown) {
            if (Player.getState() != FighterState::KnockedOut) ++Reports[Index].Knockdowns;
            Events.push_back(KnockedDown{.Fighter = Index});
        } else if (After == rig::Posture::Standing) {
            Events.push_back(GotUp{.Fighter = Index});
        }
    }

    ElapsedSec += Dt;
    ++Tick;

    const float HpLeft = Left.getHp();
    const float HpRight = Right.getHp();
    if (HpLeft <= 0.0f || HpRight <= 0.0f) {
        finish(HpLeft > 0.0f ? Winner::Left : HpRight > 0.0f ? Winner::Right : Winner::Draw, BattleEnd::Knockout);
    } else if (ElapsedSec >= Cfg.RoundTimeSec) {
        finish(HpLeft > HpRight ? Winner::Left : HpRight > HpLeft ? Winner::Right : Winner::Draw, BattleEnd::TimeUp);
    }

    publishSnapshot();
    drawDebug();
}

void Battle::finish(Winner Outcome, BattleEnd End) {
    BattleResult Final{.WinnerSide = Outcome, .End = End, .TimeSec = ElapsedSec, .Fighters = Reports};
    for (auto&& [Report, Player] : std::views::zip(Final.Fighters, Sim->Fighters)) Report.Hp = Player.getHp();
    Result = std::move(Final);
    Events.push_back(BattleOver{.WinnerSide = Outcome, .End = End});
    SettleLeftSec = Sim->Rules.Tuning.EndSettleSec;
    debug::logEvent(std::format("battle over: {}", End == BattleEnd::Knockout ? "knockout" : "time up"));
}

void Battle::settle(float Dt) {
    // A knocked-out fighter stays down by itself (Rig::setStayDown).
    if (SettleLeftSec <= 0.0f) return;
    SettleLeftSec -= Dt;

    // The bodies move on without input; hits are bumps and nothing is told.
    const std::array<Surroundings, 2> Around = {getSurroundings(0), getSurroundings(1)};
    for (auto&& [Player, Near] : std::views::zip(Sim->Fighters, Around)) Player.control({}, Near, Dt);
    Sim->Contacts.beforeStep(Sim->Fighters[0].getRig(), Sim->Fighters[1].getRig(), Dt);
    for (Fighter& Player : Sim->Fighters) Player.applyControl(Dt);
    Sim->PhysWorld.step(Dt);
    stopPosedLimbs();
    ++Tick;
    publishSnapshot();
    drawDebug();
}

void Battle::stopPosedLimbs() {
    const std::array<rig::PosedStop, 2> Stops =
        Sim->Contacts.afterStep(Sim->Fighters[0].getRig(), Sim->Fighters[1].getRig());
    for (auto&& [Player, Stop] : std::views::zip(Sim->Fighters, Stops)) Player.onPosedStop(Stop.StrikeKept);
}

std::optional<physics::PartOverlap> Battle::findDeepestOverlap() const { return Sim->PhysWorld.findDeepestOverlap(); }


std::optional<physics::PartOverlap> Battle::findWorstOverlap() const {
    return Sim->Contacts.findWorstOverlap(Sim->PhysWorld);
}

float Battle::getOverlapTolerance(const physics::PartOverlap& Overlap) const {
    return Sim->Contacts.getOverlapTolerance(Overlap);
}

Surroundings Battle::getSurroundings(size_t Index) const {
    const Fighter& Opponent = Sim->Fighters[1 - Index];
    return {.OpponentX = Opponent.getRig().getPartPosition(BodyPart::Pelvis).X,
            .OpponentDown = Opponent.getRig().getPosture() == rig::Posture::KnockedDown};
}

void Battle::publishSnapshot() {
    Snapshot.Tick = Tick;
    Snapshot.TimeLeftSec = std::max(0.0, Cfg.RoundTimeSec - ElapsedSec);
    Snapshot.Arena.HalfWidthM = Cfg.Arena.HalfWidthM;
    for (auto&& [Player, View] : std::views::zip(Sim->Fighters, Snapshot.Fighters)) Player.fillView(View);
}

void Battle::drawDebug() {
    if constexpr (FIGHTER_DEBUG) {
        // The contacts first: their panel lines (the overlap) stay near the top.
        Sim->Contacts.drawDebug(Sim->PhysWorld, ElapsedSec, PlayerNames);
        Sim->PhysWorld.drawDebug();

        for (auto&& [Player, Name, View] : std::views::zip(Sim->Fighters, PlayerNames, Snapshot.Fighters)) {
            debug::setPanel(std::format("{} action", Name), describeAction(View, Player));
            Player.drawDebug(Name);
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
            if (Body.getPosture() == rig::Posture::KnockedDown) {
                debug::setPanel(std::format("{} pelvis", Name),
                                std::format("x {:+.2f} m, ragdoll (no controller)",
                                            Body.getPartPosition(BodyPart::Pelvis).X));
            } else {
                debug::setPanel(std::format("{} pelvis", Name),
                                std::format("x {:+.2f} m, v {:+.2f} m/s (walk {:+.2f}, knockback {:+.2f})",
                                            Controller.getPositionX(), Controller.getVelocity(),
                                            Controller.getWalkVelocity(), Controller.getKnockback()));
            }
            std::string Physical;
            for (size_t Index = 0; Index < BodyPartCount; ++Index) {
                const auto Part = static_cast<BodyPart>(Index);
                if (Body.isKinematic(Part)) continue;
                if (!Physical.empty()) Physical += ' ';
                Physical += getBodyPartName(Part);
            }
            const bool Ragdoll = Body.getPosture() == rig::Posture::KnockedDown;
            debug::setPanel(std::format("{} physical", Name), Ragdoll ? "all (ragdoll)" : Physical);
            debug::setPanel(std::format("{} clip", Name), Player.describeClip());
            debug::setPanel(std::format("{} stiffness", Name), std::format("{:.2f}", Body.getStiffness()));
            debug::setPanel(std::format("{} body", Name),
                            std::format("{:.1f} kg, motors {:.0f} Nm max, gain {:.1f}/s, walk {:.2f} m/s",
                                        Body.getTotalMass(), Body.getMotorMaxTorque(), Body.getMotorGain(),
                                        Body.getWalkSpeed()));
            debug::setPanel(std::format("{} torque", Name),
                            std::format("{:.0f} Nm total", Body.getMotorTorqueSum()));
        }

        for (const auto& Recent : Sim->RecentHits) {
            debug::ScopedSide Owner(Recent.Hit.Victim.Fighter == 0 ? debug::Side::Left : debug::Side::Right);
            const Vec2 Arrow = Recent.Direction * Recent.Hit.Impulse * HitArrowScale;
            debug::drawArrow(debug::Cat::Forces, Recent.Hit.Point, Arrow, std::format("J={:.1f}", Recent.Hit.Impulse));
        }
        std::string Round = std::format("{:.1f} s left", Snapshot.TimeLeftSec);
        if (Result) Round += std::format(", over: {}", Result->End == BattleEnd::Knockout ? "knockout" : "time up");
        debug::setPanel("round", Round);
    }
}

namespace {

/// "attacking jab (startup) speed x0.80": the fighter state as the snapshot shows
/// it, with the speed of an attack and what makes the fighter slow.
std::string describeAction(const FighterView& View, const Fighter& Player) {
    constexpr std::array StateNames = {"idle",     "walking",     "crouching",  "attacking",  "blocking",
                                       "reacting", "knocked down", "getting up", "knocked out"};
    constexpr std::array PhaseNames = {"", "startup", "active", "recovery"};
    constexpr std::array ZoneNames = {"high", "mid", "low"};
    std::string Text = StateNames[static_cast<size_t>(View.State)];
    if (View.State == FighterState::Attacking) {
        Text += std::format(" {} ({}) speed x{:.2f}", View.MoveId, PhaseNames[static_cast<size_t>(View.Phase)],
                            Player.getAttackRate());
    } else if (View.State == FighterState::Blocking) {
        Text += std::format(" {}", ZoneNames[static_cast<size_t>(View.Block)]);
    } else if (View.State == FighterState::Reacting) {
        Text += std::format(" {}", getReactionLevelName(View.Reaction));
    }
    if (Player.isExhausted()) Text += ", exhausted";
    if (View.AgainstWall) Text += ", against the wall";
    return Text;
}

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

/// An upper arm or a forearm (with the weapon it holds).
std::vector<rig::HeldItem> getHeldItems(const stats::Loadout& Gear, const rig::WeaponMount& Mount) {
    std::vector<rig::HeldItem> Held;
    for (const stats::EquipmentSlot Hand : {stats::EquipmentSlot::MainHand, stats::EquipmentSlot::OffHand}) {
        const stats::EquipmentItem* Item = Gear.findInSlot(Hand);
        // A two-handed item is held by the main hand; the other one grips it.
        if (!Item || Item->Slot != Hand || (!Item->Weapon && !Item->Shield)) continue;
        rig::HeldItem& Entry = Held.emplace_back();
        Entry.Part = Hand == stats::EquipmentSlot::MainHand ? Mount.Part : Mount.OffPart;
        if (Item->Weapon) {
            Entry.WeaponReachM = Item->Weapon->ReachM;
            Entry.WeaponRadiusM = Item->Weapon->RadiusM;
            Entry.WeaponAngleDeg = Item->Weapon->AngleDeg;
        }
        if (Item->Shield) {
            Entry.ShieldLengthM = Item->Shield->LengthM;
            Entry.ShieldWidthM = Item->Shield->WidthM;
            Entry.ShieldAngleDeg = Item->Shield->AngleDeg;
            // Decision 2026-10-08: only a shield in the off hand guards.
            Entry.ShieldGuards = Hand == stats::EquipmentSlot::OffHand;
        }
    }
    return Held;
}

rig::ContactParams getContactParams(const ArenaConfig& Arena, const CombatTuning& Tuning) {
    return {.Spacing = {.ArenaHalfWidth = Arena.HalfWidthM,
                        .BodyHalfWidth = Tuning.BodyHalfWidth,
                        .SeparationSpeed = Tuning.SeparationSpeed,
                        .PosedSeparationSpeed = Tuning.PosedSeparationSpeed,
                        .PushMaxSpeed = Tuning.PushMaxSpeed,
                        .PushAcceleration = Tuning.PushAcceleration,
                        .MaxSoftOverlap = Tuning.PushSoftOverlap},
            .StopDepth = Tuning.ContactStopDepth,
            .ArmOverlapTolerance = Tuning.ArmOverlapTolerance,
            .OverlapTolerance = Tuning.OverlapTolerance};
}

rig::RigSetup makeRigSetup(const stats::PhysicalProfile& Profile, const stats::Loadout& Gear,
                           const rig::RigDef& Body, float StartX, uint8_t Index) {
    rig::RigSetup Setup;
    Setup.Origin = {StartX, SpawnLift};
    Setup.FacingRight = StartX < 0.0f;   // fighters face each other
    Setup.FighterIndex = Index;
    for (auto&& [Mass, Part] : std::views::zip(Setup.MassKg, Profile.Parts)) Mass = Part.MassKg;
    Setup.MotorMaxTorque = Profile.MotorMaxTorque;
    Setup.MotorGain = Profile.MotorGain;
    Setup.MoveSpeedScale = Profile.MoveSpeedScale;
    Setup.Held = getHeldItems(Gear, Body.Weapon);
    if (const stats::EquipmentItem* Main = Gear.findInSlot(stats::EquipmentSlot::MainHand); Main && Main->TwoHanded) {
        Setup.GripPart = Body.Weapon.OffPart;
    }
    return Setup;
}

} // namespace

} // namespace fighter::combat
