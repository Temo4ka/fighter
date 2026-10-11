#include "editor/catalog.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "combat/clip_library.hpp"
#include "combat/moveset.hpp"
#include "editor/ghost.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

namespace fighter::editor {
namespace {

/// The files of \p Dir with the extension .json, sorted by name.
std::vector<std::filesystem::path> listJsonFiles(const std::filesystem::path& Dir);
bool isHandItem(const stats::EquipmentItem& Item);

} // namespace

std::vector<FighterEntry> listFighters(const std::filesystem::path& DataDir) {
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const combat::MoveLibrary Library = combat::MoveLibrary::load(DataDir);
    std::vector<FighterEntry> Fighters;
    for (const std::filesystem::path& Path : listJsonFiles(DataDir / "fighters")) {
        try {
            const stats::FighterSheet Sheet = stats::loadFighterSheet(Path);
            const stats::ResolvedFighter Fighter = stats::resolveFighterSheet(Sheet, Catalog);
            FighterEntry Entry;
            Entry.File = Path.stem().string();
            Entry.Name = Fighter.Name;
            for (const stats::ItemRef& Item : Sheet.Items) {
                const stats::EquipmentItem* Found = Catalog.findItem(Item.Id);
                if (Found && isHandItem(*Found)) Entry.HandItems.push_back(Item);
            }
            const StanceLookup Stance = findStance(Library, Fighter.Gear);
            Entry.MoveSetId = Stance.MoveSetId;
            Entry.StanceClip = Stance.StanceClip;
            Fighters.push_back(std::move(Entry));
        } catch (const std::exception& Error) {
            throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
        }
    }
    return Fighters;
}

std::vector<HandItemEntry> listHandItems(const std::filesystem::path& DataDir) {
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    std::vector<HandItemEntry> Items;
    for (const stats::EquipmentItem& Item : Catalog.getItems()) {
        if (!isHandItem(Item)) continue;
        Items.push_back({.Id = Item.Id, .Name = Item.Name, .Shield = Item.Shield.has_value(), .TwoHanded = Item.TwoHanded});
    }
    return Items;
}

std::vector<std::string> listClips(const std::filesystem::path& DataDir) {
    std::vector<std::string> Clips;
    for (const std::filesystem::path& Path : listJsonFiles(DataDir / "poses")) Clips.push_back(Path.stem().string());
    return Clips;
}

StanceUsers findStanceUsers(const std::filesystem::path& DataDir, const std::string& ClipName) {
    StanceUsers Users;
    const combat::MoveLibrary Library = combat::MoveLibrary::load(DataDir);
    for (const combat::MoveSet& Set : Library.getSets()) {
        if (Library.getStance(Set, combat::clips::Stance) == ClipName) Users.MoveSets.push_back(Set.Id);
    }
    for (const FighterEntry& Fighter : listFighters(DataDir)) {
        if (Fighter.StanceClip == ClipName) Users.Fighters.push_back(Fighter.Name);
    }
    return Users;
}

std::vector<MoveCandidate> findMovesPlayingClip(const std::filesystem::path& DataDir, const std::string& ClipName) {
    std::vector<MoveCandidate> Moves;
    const combat::MoveLibrary Library = combat::MoveLibrary::load(DataDir);
    for (const combat::MoveDef& Move : Library.getMoves()) {
        if (Move.Clip == ClipName) {
            Moves.push_back({.MoveId = Move.Id, .Close = false});
        } else if (Move.CloseClip == ClipName) {
            Moves.push_back({.MoveId = Move.Id, .Close = true});
        }
    }
    return Moves;
}

namespace {

std::vector<std::filesystem::path> listJsonFiles(const std::filesystem::path& Dir) {
    std::vector<std::filesystem::path> Files;
    for (const auto& Entry : std::filesystem::directory_iterator(Dir)) {
        if (Entry.is_regular_file() && Entry.path().extension() == ".json") Files.push_back(Entry.path());
    }
    std::ranges::sort(Files);
    return Files;
}

bool isHandItem(const stats::EquipmentItem& Item) { return Item.Weapon.has_value() || Item.Shield.has_value(); }

} // namespace

} // namespace fighter::editor
