#include "combat/data_check.hpp"

#include <algorithm>
#include <array>
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

    fs::path DataDir;
    std::vector<DataProblem> Problems;
    std::map<std::string, std::optional<anim::Clip>> Clips;
    std::map<std::string, std::string> ClipErrors;
    std::vector<std::pair<std::string, rig::RigDef>> Rigs;   ///< The rigs that load, by file.
};

constexpr float DegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

} // namespace

std::vector<DataProblem> checkData(const fs::path& DataDir) { return Checker(DataDir).run(); }

namespace {

std::vector<DataProblem> Checker::run() {
    checkSingletons();
    checkRigs();
    checkMovesAndSets();
    checkClipWrists();
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
            if (Clip->ActiveEndSec <= Clip->ActiveBeginSec) {
                add(File, std::format("field 'clip': '{}' has an empty active phase, so the move cannot hit", Move.Clip));
            }
            if (Clip->Strikers.none()) {
                add(File, std::format("field 'clip': '{}' has no striking parts", Move.Clip));
            } else if (Move.UsesWeapon && !Clip->isStriker(BodyPart::ForearmL) && !Clip->isStriker(BodyPart::ForearmR)) {
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
