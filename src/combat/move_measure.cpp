#include "combat/move_measure.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>

#include "anim/clip.hpp"
#include "anim/layers.hpp"
#include "combat/moveset.hpp"
#include "debug/draw.hpp"
#include "stats/equipment.hpp"
#include "stats/loading.hpp"
#include "stats/validation.hpp"

namespace fighter::combat {
namespace {

/// The longest the battle may run for a stand, s (it is never reached: the
/// stand restarts the battle after every run).
constexpr double StandRoundSec = 3600.0;
/// The arena of a stand reaches this far beyond the fighters, m.
constexpr float ArenaMargin = 3.0f;
constexpr float MinArenaHalfWidth = 5.0f;
/// Marks on the drawn path: the size of the cross at the reach and the hit, m.
constexpr float MarkSize = 0.07f;

/// The input and moveset that start a move for a loadout.
struct Resolved {
    stats::Loadout Gear;
    const MoveSet* Set = nullptr;
    MoveInput Input;
};

std::optional<MoveInput> findInput(const MoveLibrary& Library, const MoveSet& Set, const MoveDef& Move);
/// The loadout of one item in the main hand; throws a clear error if the item cannot be held.
stats::Loadout makeLoadout(const stats::ItemCatalog& Catalog, const std::string& ItemId);
std::optional<Resolved> resolveWeapon(const MoveLibrary& Library, const stats::ItemCatalog& Catalog,
                                      const std::string& ItemId, const MoveDef& Move);
PlayerCommands makeCommands(const MoveInput& Input);
BodyPart getMirroredPart(BodyPart Part);
void addCandidates(std::vector<BodyPart>& Candidates, const anim::Clip& Source);

} // namespace

float getForwardDistance(Vec2 Origin, Vec2 Point, bool FacingRight) {
    return (Point.X - Origin.X) * (FacingRight ? 1.0f : -1.0f);
}

Vec2 getPartTip(const PartTransform& Part, Vec2 Pelvis, float WeaponReachM) {
    const Vec2 Axis = rotate({0.0f, 1.0f}, Part.Angle);
    const Vec2 Half = Axis * (Part.Size.Y * 0.5f);
    const Vec2 A = Part.Position + Half;
    const Vec2 B = Part.Position - Half;
    const bool UseA = (A - Pelvis).getLengthSquared() >= (B - Pelvis).getLengthSquared();
    return (UseA ? A : B) + (UseA ? Axis : -Axis) * WeaponReachM;
}

StandSetup prepareStand(const MeasureRequest& Request) {
    const std::filesystem::path& DataDir = Request.DataDir;
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const MoveLibrary Library = MoveLibrary::load(DataDir);

    const MoveDef* Move = Library.findMove(Request.MoveId);
    if (!Move) {
        throw std::runtime_error(std::format("unknown move '{}': there is no {}", Request.MoveId,
                                             (DataDir / "moves" / (Request.MoveId + ".json")).string()));
    }

    std::string WeaponId = Request.WeaponId;
    std::optional<Resolved> Found;
    if (!WeaponId.empty()) {
        Found = resolveWeapon(Library, Catalog, WeaponId, *Move);
        if (!Found) {
            const stats::EquipmentItem& Item = *Catalog.findItem(WeaponId);
            throw std::runtime_error(std::format("move '{}' is not started by any input of the moveset '{}' of '{}'",
                                                 Move->Id, Item.MoveSet.empty() ? "unarmed" : Item.MoveSet, WeaponId));
        }
    } else {
        Found = resolveWeapon(Library, Catalog, {}, *Move);
        // Not a bare-hands move: the first item whose moveset has it.
        for (const stats::EquipmentItem& Item : Catalog.getItems()) {
            if (Found) break;
            if (Item.MoveSet.empty()) continue;
            if (!(Item.Slot == stats::EquipmentSlot::MainHand || Item.Slot == stats::EquipmentSlot::OffHand)) continue;
            try {
                Found = resolveWeapon(Library, Catalog, Item.Id, *Move);
                if (Found) WeaponId = Item.Id;
            } catch (const std::runtime_error&) {
                // An item that cannot be held in the main hand is not a candidate.
            }
        }
        if (!Found) {
            throw std::runtime_error(std::format("move '{}' is not started by any input of any moveset", Move->Id));
        }
    }

    StandSetup Setup;
    Setup.Settings = loadStandConfig(DataDir / "stand.json");
    Setup.WithDummy = Request.WithDummy;
    Setup.MoveId = Move->Id;
    Setup.MoveSetId = Found->Set->Id;
    Setup.Input = makeCommands(Found->Input);
    Setup.InputText = formatMoveInput(Found->Input);
    Setup.WeaponId = WeaponId;
    if (!WeaponId.empty()) Setup.WeaponName = Catalog.findItem(WeaponId)->Name;
    const stats::WeaponProps* Weapon = Found->Gear.findWeapon();
    Setup.WeaponReachM = Move->UsesWeapon && Weapon ? Weapon->ReachM : 0.0f;

    float Distance = Setup.Settings.DummyDistanceM;
    if (Setup.Settings.UseMoveRange && Move->Intent && Move->Intent->MaxRangeM > 0.0f) {
        Distance = (Move->Intent->MinRangeM + Move->Intent->MaxRangeM) * 0.5f;
    }
    if (!Request.WithDummy) Distance = Setup.Settings.NoDummyDistanceM;
    Setup.ClipName = Move->getClip(Distance);

    for (const std::string& Name : {Move->Clip, Move->CloseClip}) {
        if (!Name.empty()) addCandidates(Setup.Candidates, anim::loadClip(DataDir / "poses" / (Name + ".json")));
    }

    BattleConfig& Config = Setup.Config;
    Config.DataDir = DataDir;
    Config.RoundTimeSec = StandRoundSec;
    Config.SpawnDistanceM = Distance;
    Config.Arena.HalfWidthM = std::max(MinArenaHalfWidth, Distance * 0.5f + ArenaMargin);
    Config.Left.Name = "Stand";
    Config.Left.Stats = Setup.Settings.Attacker;
    Config.Left.Loadout = std::move(Found->Gear);
    Config.Right.Name = "Dummy";
    Config.Right.Stats = Setup.Settings.Dummy;
    return Setup;
}

const StrikerPath* MoveMeasure::findFarthestPath() const {
    const StrikerPath* Best = nullptr;
    for (const StrikerPath& Path : Paths) {
        if (!Best || Path.ReachM > Best->ReachM) Best = &Path;
    }
    return Best;
}

float MoveMeasure::getReachM() const {
    const StrikerPath* Best = findFarthestPath();
    return Best ? Best->ReachM : 0.0f;
}

std::string describeMeasure(const MoveMeasure& Result) {
    if (!Result.Problem.empty()) return "failed: " + Result.Problem;
    if (!Result.Finished) return "running";
    std::string Text = std::format("startup {:.2f} s, active {:.2f} s, recovery {:.2f} s", Result.StartupSec,
                                   Result.ActiveSec, Result.RecoverySec);
    if (const StrikerPath* Farthest = Result.findFarthestPath(); Farthest && Result.ActiveSec > 0.0f) {
        Text += std::format("; reach {:.2f} m ({})", Farthest->ReachM, getBodyPartName(Farthest->Part));
    }
    if (Result.Hit) {
        Text += std::format("; hit {} at {:.2f} s{}", getBodyPartName(Result.HitPart), Result.HitTimeSec,
                            Result.Blocked ? " (blocked)" : "");
    } else {
        Text += "; no hit";
    }
    return Text;
}

MoveRun::MoveRun(const StandSetup& NewSetup, double NewStepSec) : Setup(NewSetup), StepSec(NewStepSec) {}

PlayerCommands MoveRun::getAttackerCommands() const {
    return Phase == Stage::Pressing ? Setup.Input : PlayerCommands{};
}

void MoveRun::observe(const Battle& After) {
    ClockSec += StepSec;
    const FighterView& Attacker = After.getSnapshot().Fighters[0];
    switch (Phase) {
        case Stage::Settling:
            if (ClockSec >= Setup.Settings.SettleSec) {
                Phase = Stage::Pressing;
                StageStartSec = ClockSec;
            }
            return;
        case Stage::Pressing: {
            if (Attacker.State == FighterState::Attacking) {
                if (Attacker.MoveId != Setup.MoveId) {
                    finish(std::format("the input {} started '{}' instead of '{}'", Setup.InputText, Attacker.MoveId,
                                       Setup.MoveId));
                    return;
                }
                Phase = Stage::Performing;
                StageStartSec = ClockSec;
                Measure.Started = true;
                Measure.FacingRight = Attacker.FacingRight;
                for (const PartTransform& Part : Attacker.Parts) {
                    if (Part.Part == BodyPart::Pelvis) Measure.PelvisStart = Part.Position;
                }
                for (const BodyPart Part : Setup.Candidates) Measure.Paths.push_back({.Part = Part});
                break;   // this step is the first sample of the move
            }
            if (ClockSec - StageStartSec > Setup.Settings.StartTimeoutSec) {
                finish(std::format("the input {} did not start a move within {:.1f} s", Setup.InputText,
                                   Setup.Settings.StartTimeoutSec));
            }
            return;
        }
        case Stage::Performing:
            break;
        case Stage::Done:
            PauseLeftSec -= static_cast<float>(StepSec);
            return;
    }

    // Performing: one sample of the move per step.
    const double TimeSec = ClockSec - StageStartSec;
    const bool Over = Attacker.State != FighterState::Attacking || Attacker.MoveId != Setup.MoveId;
    for (const BattleEvent& Event : After.getEvents()) {
        const auto* Landed = std::get_if<StrikeLanded>(&Event);
        if (!Landed || Landed->MoveId != Setup.MoveId || Landed->Contact.Attacker.Fighter != 0 || Measure.Hit) continue;
        Measure.Hit = true;
        Measure.HitTimeSec = static_cast<float>(TimeSec);
        Measure.HitPart = Landed->Contact.Victim.Part;
        Measure.HitPoint = Landed->Contact.Point;
        Measure.Damage = Landed->Damage;
        Measure.Reaction = Landed->Reaction;
        Measure.Blocked = Landed->Blocked;
    }
    if (Over || After.getResult()) {
        finish({});
        return;
    }
    switch (Attacker.Phase) {
        case AttackPhase::Startup: Measure.StartupSec += static_cast<float>(StepSec); break;
        case AttackPhase::Active: Measure.ActiveSec += static_cast<float>(StepSec); break;
        case AttackPhase::Recovery: Measure.RecoverySec += static_cast<float>(StepSec); break;
        case AttackPhase::None: break;
    }
    recordTips(Attacker);
    if (TimeSec > Setup.Settings.MoveTimeoutSec) {
        finish(std::format("the move did not end within {:.1f} s", Setup.Settings.MoveTimeoutSec));
    }
}

void MoveRun::finish(const std::string& Problem) {
    Phase = Stage::Done;
    PauseLeftSec = Setup.Settings.RepeatPauseSec;
    Measure.Problem = Problem;
    Measure.Finished = true;
    if (Measure.Started) summarize();
}

void MoveRun::summarize() {
    for (StrikerPath& Path : Measure.Paths) {
        for (const TrajectoryPoint& Point : Path.Points) {
            Path.TravelM = std::max(Path.TravelM, (Point.Tip - Path.Points.front().Tip).getLength());
        }
        for (const TrajectoryPoint& Point : Path.Points) {
            if (Point.Phase != AttackPhase::Active) continue;
            const float Forward = getForwardDistance(Measure.PelvisStart, Point.Tip, Measure.FacingRight);
            if (Forward > Path.ReachM || Path.ReachM == 0.0f) {
                Path.ReachM = Forward;
                Path.ReachPoint = Point.Tip;
                Path.ReachTimeSec = Point.TimeSec;
            }
        }
    }
    // A clip may be played mirrored (legs swapped, arms swapped): of a part
    // and its mirror keep the one whose tip travelled further.
    std::vector<bool> Drop(Measure.Paths.size(), false);
    for (const auto& [Path, Dropped] : std::views::zip(Measure.Paths, Drop)) {
        const BodyPart Other = getMirroredPart(Path.Part);
        if (Other == Path.Part) continue;
        const auto Twin = std::ranges::find(Measure.Paths, Other, &StrikerPath::Part);
        if (Twin == Measure.Paths.end()) continue;
        Dropped = Twin->TravelM != Path.TravelM ? Twin->TravelM > Path.TravelM
                                                : static_cast<size_t>(Other) < static_cast<size_t>(Path.Part);
    }
    std::vector<StrikerPath> Kept;
    for (auto&& [Path, Dropped] : std::views::zip(Measure.Paths, Drop)) {
        if (!Dropped) Kept.push_back(std::move(Path));
    }
    Measure.Paths = std::move(Kept);
}

void MoveRun::recordTips(const FighterView& Attacker) {
    Vec2 Pelvis;
    for (const PartTransform& Part : Attacker.Parts) {
        if (Part.Part == BodyPart::Pelvis) Pelvis = Part.Position;
    }
    const float TimeSec = static_cast<float>(ClockSec - StageStartSec);
    for (StrikerPath& Path : Measure.Paths) {
        const PartTransform& Part = Attacker.Parts[static_cast<size_t>(Path.Part)];
        const float WeaponReach = Path.Part == stats::getHandPart(stats::EquipmentSlot::MainHand) ? Setup.WeaponReachM : 0.0f;
        Path.Points.push_back({.TimeSec = TimeSec, .Tip = getPartTip(Part, Pelvis, WeaponReach), .Phase = Attacker.Phase});
    }
}

MoveMeasure measureMove(const MeasureRequest& Request) {
    const StandSetup Setup = prepareStand(Request);
    Battle Fight(Setup.Config);
    MoveRun Run(Setup);
    // The run ends itself (a move that does not start or end is a timeout), so
    // this loop ends too; the bound only guards a bug.
    constexpr double Step = 1.0 / 60.0;
    const double LimitSec = Setup.Settings.SettleSec + Setup.Settings.StartTimeoutSec + Setup.Settings.MoveTimeoutSec + 1.0;
    for (double Elapsed = 0.0; !Run.isFinished() && Elapsed < LimitSec; Elapsed += Step) {
        Fight.update(Run.getAttackerCommands(), PlayerCommands{}, Step);
        Run.observe(Fight);
    }
    return Run.getMeasure();
}

void drawMeasure(const StandSetup& Setup, const MoveMeasure& Result) {
    if constexpr (FIGHTER_DEBUG) {
        using debug::Cat;
        debug::setPanel("stand move", std::format("{} ({}), input {}", Setup.MoveId, Setup.ClipName, Setup.InputText));
        debug::setPanel("stand set", Setup.MoveSetId);
        debug::setPanel("stand weapon", Setup.WeaponId.empty()
                                            ? std::string("bare hands")
                                            : std::format("{} ({}), reach {:.2f} m", Setup.WeaponName, Setup.WeaponId,
                                                          Setup.WeaponReachM));
        debug::setPanel("stand result", describeMeasure(Result));

        for (const StrikerPath& Path : Result.Paths) {
            for (const auto& [From, To] : std::views::zip(Path.Points, Path.Points | std::views::drop(1))) {
                debug::drawLine(To.Phase == AttackPhase::Active ? Cat::Hitbox : Cat::Trajectory, From.Tip, To.Tip);
            }
            if (!Path.Points.empty()) {
                debug::drawText(Cat::Trajectory, Path.Points.front().Tip, std::format("{} start", getBodyPartName(Path.Part)));
            }
            if (Result.Finished && Result.ActiveSec > 0.0f && &Path == Result.findFarthestPath()) {
                debug::drawCross(Cat::Trajectory, Path.ReachPoint, MarkSize);
                debug::drawLine(Cat::Trajectory, {Result.PelvisStart.X, Path.ReachPoint.Y}, Path.ReachPoint);
                debug::drawText(Cat::Trajectory, Path.ReachPoint,
                                std::format("reach {:.2f} m at {:.2f} s", Path.ReachM, Path.ReachTimeSec));
            }
        }
        if (Result.Started) debug::drawCross(Cat::Trajectory, Result.PelvisStart, MarkSize);
        if (Result.Hit) {
            debug::drawCross(Cat::Hitbox, Result.HitPoint, MarkSize);
            debug::drawText(Cat::Hitbox, Result.HitPoint,
                            std::format("hit {} {:.2f} s", getBodyPartName(Result.HitPart), Result.HitTimeSec));
        }
    }
}

namespace {

std::optional<MoveInput> findInput(const MoveLibrary& Library, const MoveSet& Set, const MoveDef& Move) {
    for (const MoveSet* Current = &Set; Current; Current = Library.findSet(Current->Inherit)) {
        for (const MoveSetEntry& Entry : Current->Entries) {
            if (Entry.MoveId != Move.Id) continue;
            // A line shadowed by a child's line for the same input does not start the move.
            const MoveDef* Chosen = Library.findMove(Set, Entry.Input.Direction, Entry.Input.Buttons, Entry.Input.Buttons);
            if (Chosen == &Move) return Entry.Input;
        }
        if (Current->Inherit.empty()) break;
    }
    return std::nullopt;
}

stats::Loadout makeLoadout(const stats::ItemCatalog& Catalog, const std::string& ItemId) {
    if (!Catalog.findItem(ItemId)) {
        throw std::runtime_error(std::format("unknown item '{}': it is not in data/items/", ItemId));
    }
    const std::vector<stats::ItemRef> Items = {{.Id = ItemId, .Slot = stats::EquipmentSlot::MainHand}};
    try {
        return stats::buildLoadout(Items, Catalog);
    } catch (const stats::DataError& Error) {
        throw std::runtime_error(std::format("item '{}' cannot be held in the main hand: {}", ItemId, Error.what()));
    }
}

std::optional<Resolved> resolveWeapon(const MoveLibrary& Library, const stats::ItemCatalog& Catalog,
                                      const std::string& ItemId, const MoveDef& Move) {
    Resolved Result;
    if (!ItemId.empty()) Result.Gear = makeLoadout(Catalog, ItemId);
    Result.Set = &Library.selectSet(Result.Gear.getMoveSet(stats::EquipmentSlot::MainHand),
                                    Result.Gear.getMoveSet(stats::EquipmentSlot::OffHand), false);
    const std::optional<MoveInput> Input = findInput(Library, *Result.Set, Move);
    if (!Input) return std::nullopt;
    Result.Input = *Input;
    return Result;
}

PlayerCommands makeCommands(const MoveInput& Input) {
    PlayerCommands Cmd;
    switch (Input.Direction) {
        case InputDirection::Forward:
        case InputDirection::UpForward:
        case InputDirection::DownForward: Cmd.MoveX = 1.0f; break;
        case InputDirection::Back:
        case InputDirection::UpBack:
        case InputDirection::DownBack: Cmd.MoveX = -1.0f; break;
        default: break;
    }
    Cmd.Up = Input.Direction == InputDirection::Up || Input.Direction == InputDirection::UpForward ||
             Input.Direction == InputDirection::UpBack;
    Cmd.Down = isDownward(Input.Direction);
    Cmd.Light = Input.Buttons.contains(AttackButton::Light);
    Cmd.Heavy = Input.Buttons.contains(AttackButton::Heavy);
    Cmd.Kick = Input.Buttons.contains(AttackButton::Kick);
    Cmd.Special = Input.Buttons.contains(AttackButton::Special);
    return Cmd;
}

BodyPart getMirroredPart(BodyPart Part) {
    std::bitset<BodyPartCount> Single;
    Single.set(static_cast<size_t>(Part));
    const std::bitset<BodyPartCount> Mirrored = anim::mirrorArmParts(anim::mirrorLegParts(Single));
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (Mirrored.test(Index)) return static_cast<BodyPart>(Index);
    }
    return Part;
}

void addCandidates(std::vector<BodyPart>& Candidates, const anim::Clip& Source) {
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const auto Part = static_cast<BodyPart>(Index);
        if (!Source.isStriker(Part)) continue;
        for (const BodyPart Each : {Part, getMirroredPart(Part)}) {
            if (std::ranges::find(Candidates, Each) == Candidates.end()) Candidates.push_back(Each);
        }
    }
}

} // namespace

} // namespace fighter::combat
