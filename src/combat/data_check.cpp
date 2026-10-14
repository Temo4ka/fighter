#include "combat/data_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "anim/clip.hpp"
#include "anim/layers.hpp"
#include "combat/clip_library.hpp"
#include "combat/move_input.hpp"
#include "combat/moves.hpp"
#include "combat/reactions.hpp"
#include "combat/stand_config.hpp"
#include "combat/tuning.hpp"
#include "core/text_file.hpp"
#include "rig/rig_def.hpp"
#include "stats/equipment.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

namespace fighter::combat {
namespace {

namespace fs = std::filesystem;

/// Everything checkData() collects while it goes through the files.
class Checker {
public:
    explicit Checker(fs::path Dir) : DataDir(std::move(Dir)) {}

    std::vector<DataProblem> run();

private:
    /// A clip of poses/ by name, loaded once; nullptr (and the reason in
    /// \p Why) if it cannot be loaded.
    const anim::Clip* findClip(const std::string& Name, std::string& Why);
    /// Reports a clip that a field names and cannot be loaded; returns the
    /// clip or nullptr.
    const anim::Clip* requireClip(const std::string& File, const std::string& Field, const std::string& Name);

    void add(std::string File, std::string Detail) { Problems.push_back({std::move(File), std::move(Detail)}); }
    void warn(std::string File, std::string Detail) {
        Problems.push_back({.File = std::move(File), .Detail = std::move(Detail), .Warning = true});
    }
    /// Reports \p Error as a problem of \p File, without the path the
    /// loaders put in front of the message.
    void addError(const std::string& File, const std::exception& Error);
    std::string relative(const fs::path& Path) const;
    /// The *.json files of \p SubDir in name order; a missing directory is a problem.
    std::vector<fs::path> listFiles(const std::string& SubDir);

    template <class Callable>
    void guard(const fs::path& File, Callable&& Body) {
        try {
            Body();
        } catch (const std::exception& Error) {
            addError(relative(File), Error);
        }
    }

    void checkSingletons();
    void checkRigs();
    void checkMovesAndSets();
    void checkMoveFiles(const MoveLibrary& Library, const std::set<std::string>& Stubs);
    void checkMoveSetFiles(const MoveLibrary& Library, const std::set<std::string>& Stubs);
    void checkStateClips();
    void checkItemsAndFighters(const std::set<std::string>& KnownSets);
    /// The wrist angles of the loaded clips (the key "Weapon") within the
    /// wrist limits of every rig.
    void checkClipWrists();
    /// Is \p Degrees outside the wrist limits of a rig? Reports it to \p File
    /// with \p What in front and returns true.
    bool checkWristAngle(const std::string& File, const std::string& What, float Degrees);
    /// The joint angles of the loaded clips within the joint limits of every
    /// rig, and no faster than the arm motors turn (a warning).
    void checkClipJoints();
    /// The clips the state machine plays as a pose of the legs set the legs,
    /// those it plays over the walk do not (warnings).
    void checkStateClipLayers(const MoveLibrary& Library);
    /// Each attack of a moveset starts and ends in the guard of the set's
    /// stance (a warning).
    void checkAttackEdges(const MoveLibrary& Library, const std::set<std::string>& Stubs);

    fs::path DataDir;
    std::vector<DataProblem> Problems;
    std::map<std::string, std::optional<anim::Clip>> Clips;
    std::map<std::string, std::string> ClipErrors;
    std::vector<std::pair<std::string, rig::RigDef>> Rigs;   ///< The rigs that load, by file.
};

constexpr float DegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
/// A clip angle off a joint limit by rounding only is not outside it, rad.
constexpr float JointLimitSlack = 1e-4f;
/// A clip may ask an arm joint for this many times the motors' speed
/// (maxJointSpeed) before the arm visibly lags behind it.
constexpr float JointSpeedAllowance = 1.5f;
/// The first and the last key of an attack are this close to the stance's
/// guard, rad (0.5 deg): an attack layered over the stance does not jump
/// when it starts or ends.
constexpr float StanceEdgeTolerance = 0.5f / DegreesPerRadian;

} // namespace

std::vector<DataProblem> checkData(const fs::path& DataDir) { return Checker(DataDir).run(); }

StrikeProblems findStrikeProblems(const anim::Clip& Clip, bool UsesWeapon) {
    StrikeProblems Problems;
    Problems.EmptyActive = Clip.ActiveEndSec <= Clip.ActiveBeginSec;
    Problems.NoStrikers = Clip.Strikers.none();
    Problems.WeaponNotHeld = !Problems.NoStrikers && UsesWeapon && !Clip.isStriker(BodyPart::ForearmL) &&
                             !Clip.isStriker(BodyPart::ForearmR);
    return Problems;
}

namespace {

std::vector<DataProblem> Checker::run() {
    checkSingletons();
    checkRigs();
    checkMovesAndSets();
    checkClipWrists();
    checkClipJoints();
    return std::move(Problems);
}

std::string Checker::relative(const fs::path& Path) const {
    return Path.lexically_relative(DataDir).generic_string();
}

void Checker::addError(const std::string& File, const std::exception& Error) {
    std::string Message = Error.what();
    // The loaders start with the path of the file, which File already says.
    for (const std::string& Prefix : {(DataDir / File).string() + ": ", File + ": "}) {
        if (Message.starts_with(Prefix)) {
            Message.erase(0, Prefix.size());
            break;
        }
    }
    add(File, std::move(Message));
}

std::vector<fs::path> Checker::listFiles(const std::string& SubDir) {
    std::vector<fs::path> Files;
    std::error_code Error;
    for (const auto& Entry : fs::directory_iterator(DataDir / SubDir, Error)) {
        if (Entry.is_regular_file() && Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    if (Error) {
        add(SubDir, std::format("cannot list the directory: {}", Error.message()));
        return {};
    }
    std::ranges::sort(Files);
    return Files;
}

const anim::Clip* Checker::findClip(const std::string& Name, std::string& Why) {
    auto Found = Clips.find(Name);
    if (Found == Clips.end()) {
        std::optional<anim::Clip> Loaded;
        const fs::path File = DataDir / "poses" / (Name + ".json");
        std::error_code Error;
        if (!fs::exists(File, Error)) {
            ClipErrors[Name] = std::format("there is no file poses/{}.json", Name);
        } else {
            try {
                Loaded = anim::loadClip(File);
            } catch (const std::exception& Failure) {
                std::string Message = Failure.what();
                const std::string Prefix = File.string() + ": ";
                if (Message.starts_with(Prefix)) Message.erase(0, Prefix.size());
                ClipErrors[Name] = std::format("poses/{}.json: {}", Name, Message);
            }
        }
        Found = Clips.emplace(Name, std::move(Loaded)).first;
    }
    if (!Found->second) {
        Why = ClipErrors[Name];
        return nullptr;
    }
    return &*Found->second;
}

const anim::Clip* Checker::requireClip(const std::string& File, const std::string& Field, const std::string& Name) {
    std::string Why;
    const anim::Clip* Found = findClip(Name, Why);
    if (!Found) add(File, std::format("field '{}': clip '{}': {}", Field, Name, Why));
    return Found;
}

void Checker::checkSingletons() {
    guard(DataDir / "balance.json", [&] { stats::loadBalanceTable(DataDir / "balance.json"); });
    guard(DataDir / "combat.json", [&] { loadCombatTuning(DataDir / "combat.json"); });
    guard(DataDir / "stand.json", [&] { loadStandConfig(DataDir / "stand.json"); });
    guard(DataDir / "reactions.json", [&] { loadReactionTable(DataDir / "reactions.json"); });
}

void Checker::checkRigs() {
    for (const fs::path& File : listFiles("rigs")) {
        guard(File, [&] { Rigs.emplace_back(relative(File), rig::loadRigDef(File)); });
    }
}

bool Checker::checkWristAngle(const std::string& File, const std::string& What, float Degrees) {
    for (const auto& [RigFile, Def] : Rigs) {
        const float Lower = Def.Weapon.WristLowerAngle * DegreesPerRadian;
        const float Upper = Def.Weapon.WristUpperAngle * DegreesPerRadian;
        // A float off by rounding is not outside.
        constexpr float Slack = 1e-3f;
        if (Degrees >= Lower - Slack && Degrees <= Upper + Slack) continue;
        add(File, std::format("{} {:g} deg is outside the wrist limits [{:g}, {:g}] of {} (weapon.wristLimits); "
                              "the rig clamps it",
                              What, Degrees, Lower, Upper, RigFile));
        return true;
    }
    return false;
}

void Checker::checkClipWrists() {
    for (const auto& [Name, Clip] : Clips) {
        if (!Clip) continue;
        for (const anim::Keyframe& Key : Clip->Keys) {
            if (!Key.Target.HasWeapon) continue;
            const std::string What = std::format("key at t = {:g} s: '{}'", Key.TimeSec, anim::WeaponKey);
            // One report per clip is enough.
            if (checkWristAngle(std::format("poses/{}.json", Name), What, Key.Target.WeaponAngle * DegreesPerRadian)) {
                break;
            }
        }
    }
}

void Checker::checkClipJoints() {
    for (const auto& [Name, Clip] : Clips) {
        if (!Clip) continue;
        const std::string File = std::format("poses/{}.json", Name);
        for (const auto& [RigFile, Def] : Rigs) {
            // One report of each kind per clip and rig is enough.
            bool Reported = false;
            for (const anim::Keyframe& Key : Clip->Keys) {
                for (const rig::JointDef& Joint : Def.Joints) {
                    if (Reported || !Key.Target.hasJoint(Joint.Child)) continue;
                    const float Angle = Key.Target.getAngle(Joint.Child);
                    if (Angle >= Joint.LowerAngle - JointLimitSlack && Angle <= Joint.UpperAngle + JointLimitSlack) {
                        continue;
                    }
                    add(File, std::format("key at t = {:g} s: '{}' {:g} deg is outside the joint limits [{:g}, {:g}] "
                                          "of {}; the rig clamps it",
                                          Key.TimeSec, getBodyPartName(Joint.Child), Angle * DegreesPerRadian,
                                          Joint.LowerAngle * DegreesPerRadian, Joint.UpperAngle * DegreesPerRadian,
                                          RigFile));
                    Reported = true;
                }
            }
            // The legs are posed directly, without motors; a loop has no
            // fast swing to lag behind.
            if (Clip->Loop) continue;
            const float Limit = Def.Control.MaxJointSpeed * JointSpeedAllowance;
            Reported = false;
            for (size_t Index = 1; Index < Clip->Keys.size() && !Reported; ++Index) {
                const anim::Keyframe& Before = Clip->Keys[Index - 1];
                const anim::Keyframe& After = Clip->Keys[Index];
                const float Span = After.TimeSec - Before.TimeSec;
                if (Span <= 0.0f) continue;
                for (size_t Part = 0; Part < BodyPartCount && !Reported; ++Part) {
                    const auto Joint = static_cast<BodyPart>(Part);
                    if (!After.Target.hasJoint(Joint) || anim::getLayer(Joint) == anim::Layer::Legs) continue;
                    const float Speed = std::abs(After.Target.Angles[Part] - Before.Target.Angles[Part]) / Span;
                    if (Speed <= Limit) continue;
                    warn(File, std::format("key at t = {:g} s: '{}' turns at {:.1f} rad/s, more than {:g} x the "
                                           "motors' maxJointSpeed ({:g} rad/s) of {}: the arm lags behind",
                                           After.TimeSec, getBodyPartName(Joint), Speed, JointSpeedAllowance,
                                           Def.Control.MaxJointSpeed, RigFile));
                    Reported = true;
                }
            }
        }
    }
}

void Checker::checkStateClipLayers(const MoveLibrary& Library) {
    // docs/MECHANICS.md, section 2.5: a clip with a leg joint is an action
    // of the legs; without one it plays over the walk or the rest pose.
    std::set<std::string> WithLegs;
    for (const std::string_view Name : {clips::Stance, clips::Walk, clips::Crouch, clips::CrouchWalk, clips::BlockLow}) {
        WithLegs.emplace(Name);
    }
    std::set<std::string> UpperOnly;
    for (const std::string_view Name :
         {clips::BlockHigh, clips::BlockMid, clips::Flinch, clips::Stagger, clips::Knockback}) {
        UpperOnly.emplace(Name);
    }
    for (const MoveSet& Set : Library.getSets()) {
        if (Set.Stance) WithLegs.insert(*Set.Stance);
        for (const BlockZone Zone : {BlockZone::High, BlockZone::Mid, BlockZone::Low}) {
            const std::optional<std::string>& Name = Set.Block.Clips[static_cast<size_t>(Zone)];
            if (!Name || Name->empty()) continue;
            (Zone == BlockZone::Low ? WithLegs : UpperOnly).insert(*Name);
        }
    }
    std::string Why;
    for (const std::string& Name : WithLegs) {
        const anim::Clip* Clip = findClip(Name, Why);
        if (Clip && !anim::usesLegs(*Clip)) {
            warn(std::format("poses/{}.json", Name), "the state machine plays it as a pose of the legs, but it sets "
                                                     "no leg joint: the legs walk or rest under it");
        }
    }
    for (const std::string& Name : UpperOnly) {
        const anim::Clip* Clip = findClip(Name, Why);
        if (Clip && anim::usesLegs(*Clip)) {
            warn(std::format("poses/{}.json", Name), "the state machine plays it over the walk, but it sets leg "
                                                     "joints: it becomes an action of the legs");
        }
    }
}

void Checker::checkAttackEdges(const MoveLibrary& Library, const std::set<std::string>& Stubs) {
    // A weapon is held by the main hand; a clip authored for the other arm
    // plays with the arms swapped (Fighter::chooseStrikeWeapon).
    std::optional<BodyPart> MainHand;
    if (!Rigs.empty()) MainHand = Rigs.front().second.Weapon.Part;
    std::set<std::string> Seen;
    std::string Why;
    for (const MoveSet& Set : Library.getSets()) {
        if (Stubs.contains("movesets/" + Set.Id)) continue;
        // MoveLibrary::getStance() would go round parents in a circle.
        std::set<std::string> Visited;
        std::optional<std::string> StanceName;
        for (const MoveSet* Current = &Set; Current && Visited.insert(Current->Id).second;
             Current = Current->Inherit.empty() ? nullptr : Library.findSet(Current->Inherit)) {
            if (Current->Stance) {
                StanceName = *Current->Stance;
                break;
            }
            if (Current->Inherit.empty()) StanceName = std::string(clips::Stance);
        }
        if (!StanceName) continue;
        const anim::Clip* Stance = findClip(*StanceName, Why);
        if (!Stance || Stance->Keys.empty()) continue;
        for (const MoveSetEntry& Entry : Set.Entries) {
            const MoveDef* Move = Library.findMove(Entry.MoveId);
            const anim::Clip* Attack = Move ? findClip(Move->Clip, Why) : nullptr;
            if (!Attack || Attack->Keys.empty()) continue;
            const bool OtherHand = Move->UsesWeapon && MainHand && !Attack->isStriker(*MainHand) &&
                                   Attack->isStriker(anim::getMirroredArmPart(*MainHand));
            if (!Seen.insert(std::format("{}|{}|{}", Move->Clip, *StanceName, OtherHand)).second) continue;
            const anim::Pose& Authored = Stance->Keys.front().Target;
            const anim::Pose Guard = OtherHand ? anim::mirrorArms(Authored) : Authored;
            // The legs step into an action's pose (legStep), so only the
            // upper body has to match.
            for (const bool First : {true, false}) {
                const anim::Keyframe& Edge = First ? Attack->Keys.front() : Attack->Keys.back();
                std::optional<BodyPart> Worst;
                float WorstOff = StanceEdgeTolerance;
                for (size_t Part = 0; Part < BodyPartCount; ++Part) {
                    const auto Joint = static_cast<BodyPart>(Part);
                    if (!Edge.Target.hasJoint(Joint) || !Guard.hasJoint(Joint)) continue;
                    if (anim::getLayer(Joint) == anim::Layer::Legs) continue;
                    const float Off = std::abs(Edge.Target.Angles[Part] - Guard.Angles[Part]);
                    if (Off <= WorstOff) continue;
                    WorstOff = Off;
                    Worst = Joint;
                }
                if (!Worst) continue;
                warn(std::format("poses/{}.json", Move->Clip),
                     std::format("the {} key (t = {:g} s) is {:.1f} deg off the guard of the stance '{}' (moveset "
                                 "'{}'{}) in '{}': the body jumps when the attack {}",
                                 First ? "first" : "last", Edge.TimeSec, WorstOff * DegreesPerRadian, *StanceName,
                                 Set.Id, OtherHand ? ", arms swapped" : "", getBodyPartName(*Worst),
                                 First ? "starts" : "ends"));
            }
        }
    }
}

void Checker::checkMovesAndSets() {
    // Files that cannot be read stay in the library as empty stubs, so that
    // the references to them are not reported again as missing.
    std::set<std::string> Stubs;
    std::vector<MoveDef> Moves;
    for (const fs::path& File : listFiles("moves")) {
        const std::string Id = File.stem().string();
        try {
            Moves.push_back(parseMoveDef(readTextFile(File), Id));
        } catch (const std::exception& Error) {
            addError(relative(File), Error);
            Moves.push_back({.Id = Id});
            Stubs.insert("moves/" + Id);
        }
    }
    std::vector<MoveSet> Sets;
    for (const fs::path& File : listFiles("movesets")) {
        const std::string Id = File.stem().string();
        try {
            Sets.push_back(parseMoveSet(readTextFile(File), Id));
        } catch (const std::exception& Error) {
            addError(relative(File), Error);
            Sets.push_back({.Id = Id});
            Stubs.insert("movesets/" + Id);
        }
    }
    InputRules Input = InputRules::getDefaults();
    guard(DataDir / "input.json", [&] { Input = loadInputRules(DataDir / "input.json"); });

    std::set<std::string> KnownSets;
    for (const MoveSet& Set : Sets) KnownSets.insert(Set.Id);

    const MoveLibrary Library = MoveLibrary::buildUnchecked(std::move(Moves), std::move(Sets), std::move(Input));
    for (const DataProblem& Problem : Library.findProblems()) Problems.push_back(Problem);
    checkMoveFiles(Library, Stubs);
    checkMoveSetFiles(Library, Stubs);
    checkStateClips();
    checkStateClipLayers(Library);
    checkAttackEdges(Library, Stubs);
    checkItemsAndFighters(KnownSets);
}

void Checker::checkMoveFiles(const MoveLibrary& Library, const std::set<std::string>& Stubs) {
    for (const MoveDef& Move : Library.getMoves()) {
        const std::string File = std::format("moves/{}.json", Move.Id);
        if (Stubs.contains("moves/" + Move.Id)) continue;
        for (const std::string& Next : Move.ChainTo) {
            if (!Library.findMove(Next)) add(File, std::format("field 'chain_to': there is no move '{}'", Next));
        }
        if (const anim::Clip* Clip = requireClip(File, "clip", Move.Clip)) {
            const StrikeProblems Strike = findStrikeProblems(*Clip, Move.UsesWeapon);
            if (Strike.EmptyActive) {
                add(File, std::format("field 'clip': '{}' has an empty active phase, so the move cannot hit", Move.Clip));
            }
            if (Strike.NoStrikers) {
                add(File, std::format("field 'clip': '{}' has no striking parts", Move.Clip));
            } else if (Strike.WeaponNotHeld) {
                add(File, std::format("field 'uses_weapon': the weapon is held by a forearm, but '{}' strikes with "
                                      "other parts only",
                                      Move.Clip));
            }
        }
        if (!Move.CloseClip.empty()) requireClip(File, "close_clip", Move.CloseClip);
    }
}

void Checker::checkMoveSetFiles(const MoveLibrary& Library, const std::set<std::string>& Stubs) {
    static constexpr std::array<std::string_view, BlockZoneCount> ZoneNames = {"High", "Mid", "Low"};
    for (const MoveSet& Set : Library.getSets()) {
        if (Stubs.contains("movesets/" + Set.Id)) continue;
        const std::string File = std::format("movesets/{}.json", Set.Id);
        if (Set.Stance) {
            const anim::Clip* Clip = requireClip(File, "stance", *Set.Stance);
            if (Clip && !Clip->PelvisTrack.empty()) {
                add(File, std::format("field 'stance': '{}' has a pelvis track (pelvisX), but only attacks move "
                                      "the pelvis",
                                      *Set.Stance));
            }
        }
        for (size_t Zone = 0; Zone < BlockZoneCount; ++Zone) {
            if (const auto& Name = Set.Block.Clips[Zone]; Name && !Name->empty()) {
                const anim::Clip* Clip = requireClip(File, std::format("block.clips.{}", ZoneNames[Zone]), *Name);
                if (Clip && !Clip->PelvisTrack.empty()) {
                    add(File, std::format("field 'block.clips.{}': '{}' has a pelvis track (pelvisX), but only "
                                          "attacks move the pelvis",
                                          ZoneNames[Zone], *Name));
                }
            }
        }
    }
}

void Checker::checkStateClips() {
    for (const std::string_view Name : {clips::Stance, clips::Walk, clips::Crouch, clips::CrouchWalk, clips::BlockHigh,
                                        clips::BlockMid, clips::BlockLow, clips::Flinch, clips::Stagger,
                                        clips::Knockback}) {
        std::string Why;
        const anim::Clip* Clip = findClip(std::string(Name), Why);
        if (!Clip) {
            add(std::format("poses/{}.json", Name), std::format("a clip the state machine plays: {}", Why));
        } else if (!Clip->PelvisTrack.empty()) {
            add(std::format("poses/{}.json", Name),
                "field 'pelvisX': a clip the state machine plays has a pelvis track, but only attacks move the pelvis");
        }
    }
}

void Checker::checkItemsAndFighters(const std::set<std::string>& KnownSets) {
    stats::ItemCatalog Catalog;
    for (const fs::path& File : listFiles("items")) {
        guard(File, [&] {
            const stats::ItemCatalog Part = stats::parseItemCatalog(readTextFile(File), relative(File));
            for (const stats::EquipmentItem& Item : Part.getItems()) {
                try {
                    Catalog.addItem(Item);
                } catch (const std::exception& Error) {
                    add(relative(File), std::format("item '{}': {}", Item.Id, Error.what()));
                    continue;
                }
                if (Item.Weapon && Item.Weapon->AngleDeg) {
                    checkWristAngle(relative(File), std::format("item '{}': field 'weapon.angle_deg':", Item.Id),
                                    *Item.Weapon->AngleDeg);
                }
                if (!Item.MoveSet.empty() && !KnownSets.contains(Item.MoveSet)) {
                    add(relative(File), std::format("item '{}': field 'moveset': there is no moveset '{}'", Item.Id,
                                                    Item.MoveSet));
                }
            }
        });
    }
    for (const fs::path& File : listFiles("fighters")) {
        guard(File, [&] {
            const stats::FighterSheet Sheet = stats::parseFighterSheet(readTextFile(File), relative(File));
            stats::resolveFighterSheet(Sheet, Catalog);
        });
    }
}

} // namespace

} // namespace fighter::combat
