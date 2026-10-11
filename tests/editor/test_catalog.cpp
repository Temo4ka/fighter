#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "anim/clip.hpp"
#include "editor/catalog.hpp"
#include "editor/ghost.hpp"

using namespace fighter;
using namespace fighter::editor;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

const FighterEntry& getFighter(const std::vector<FighterEntry>& Fighters, const std::string& File) {
    const auto Found = std::ranges::find(Fighters, File, &FighterEntry::File);
    REQUIRE(Found != Fighters.end());
    return *Found;
}

bool contains(const std::vector<std::string>& Names, const std::string& Name) {
    return std::ranges::find(Names, Name) != Names.end();
}

} // namespace

TEST_CASE("Every fighter file resolves to its items, moveset and stance clip", "[editor][catalog]") {
    const std::vector<FighterEntry> Fighters = listFighters(DataDir);
    size_t Files = 0;
    for (const auto& File : std::filesystem::directory_iterator(DataDir / "fighters")) {
        if (File.path().extension() == ".json") ++Files;
    }
    CHECK(Fighters.size() == Files);
    for (const FighterEntry& Fighter : Fighters) {
        INFO(Fighter.File);
        CHECK_FALSE(Fighter.Name.empty());
        CHECK_FALSE(Fighter.MoveSetId.empty());
        // The stance is a clip of data/poses that loads...
        CHECK(std::filesystem::exists(DataDir / "poses" / (Fighter.StanceClip + ".json")));
        // ...and is the one the ghost shows for the items the fighter holds.
        const GhostContext Context = loadGhostContext(DataDir.parent_path(), Fighter.HandItems);
        CHECK(Context.StanceName == Fighter.StanceClip);
        CHECK(Context.MoveSetId == Fighter.MoveSetId);
    }
}

TEST_CASE("The stance of each known fighter follows its weapon's moveset", "[editor][catalog]") {
    const std::vector<FighterEntry> Fighters = listFighters(DataDir);
    const FighterEntry& Knight = getFighter(Fighters, "knight");
    CHECK(Knight.Name == "Knight");
    REQUIRE(Knight.HandItems.size() == 1);
    CHECK(Knight.HandItems.front().Id == "war_hammer");
    CHECK(Knight.MoveSetId == "hammer");
    CHECK(Knight.StanceClip == "stance_hammer");
    CHECK(getFighter(Fighters, "flamberge").StanceClip == "stance_greatsword");
    CHECK(getFighter(Fighters, "rogue").StanceClip == "stance_sword");
    // A sword and a shield: the pair set, which inherits the sword's stance.
    const FighterEntry& Shieldman = getFighter(Fighters, "shieldman");
    CHECK(Shieldman.HandItems.size() == 2);
    CHECK(Shieldman.MoveSetId == "sword_shield");
    CHECK(Shieldman.StanceClip == "stance_sword");
}

TEST_CASE("The users of a stance are the fighters and movesets that stand in it", "[editor][catalog]") {
    const StanceUsers Hammer = findStanceUsers(DataDir, "stance_hammer");
    CHECK(Hammer.Fighters == std::vector<std::string>{"Knight"});
    CHECK(Hammer.MoveSets == std::vector<std::string>{"hammer"});
    const StanceUsers Sword = findStanceUsers(DataDir, "stance_sword");
    CHECK(contains(Sword.Fighters, "Rogue"));
    CHECK(contains(Sword.Fighters, "Shieldman"));
    CHECK(contains(Sword.MoveSets, "sword"));
    CHECK(contains(Sword.MoveSets, "sword_shield"));   // inherits the stance
    // The general stance belongs to the sets that name none.
    CHECK(contains(findStanceUsers(DataDir, "stance").MoveSets, "unarmed"));
    // A stance of the shield set that no fighter stands in; a clip that is no stance.
    CHECK(findStanceUsers(DataDir, "stance_shield").Fighters.empty());
    CHECK(findStanceUsers(DataDir, "jab").isEmpty());
}

TEST_CASE("The lists of items and clips come from the data directory", "[editor][catalog]") {
    const std::vector<HandItemEntry> Items = listHandItems(DataDir);
    const auto Hammer = std::ranges::find(Items, "war_hammer", &HandItemEntry::Id);
    REQUIRE(Hammer != Items.end());
    CHECK(Hammer->TwoHanded);
    const auto Shield = std::ranges::find(Items, "wooden_shield", &HandItemEntry::Id);
    REQUIRE(Shield != Items.end());
    CHECK(Shield->Shield);
    CHECK(std::ranges::find(Items, "iron_helmet", &HandItemEntry::Id) == Items.end());   // armor is not held

    const std::vector<std::string> Clips = listClips(DataDir);
    CHECK(std::ranges::is_sorted(Clips));
    CHECK(contains(Clips, "stance_hammer"));
    CHECK(contains(Clips, "jab"));
}

TEST_CASE("The moves that play a clip are found by clip and close_clip", "[editor][catalog]") {
    const std::vector<MoveCandidate> Jab = findMovesPlayingClip(DataDir, "jab");
    REQUIRE_FALSE(Jab.empty());
    CHECK(Jab.front() == MoveCandidate{.MoveId = "jab", .Close = false});
    const std::vector<MoveCandidate> Close = findMovesPlayingClip(DataDir, "jab_close");
    REQUIRE_FALSE(Close.empty());
    CHECK(Close.front() == MoveCandidate{.MoveId = "jab", .Close = true});
    CHECK(findMovesPlayingClip(DataDir, "stance_hammer").empty());
    CHECK(findMovesPlayingClip(DataDir, "no_such_clip").empty());
}
