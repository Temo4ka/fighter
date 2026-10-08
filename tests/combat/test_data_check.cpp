#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "combat/data_check.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;

namespace {

/// Does a problem of \p File (relative to the data directory) mention \p Text?
bool hasProblem(const std::vector<DataProblem>& Problems, const std::string& File, const std::string& Text) {
    return std::ranges::any_of(Problems, [&](const DataProblem& Problem) {
        return Problem.File == File && Problem.Detail.find(Text) != std::string::npos;
    });
}

/// A move file with the required fields and \p Extra.
std::string moveJson(const std::string& Extra) {
    return "{\"damage\": 1.0, \"min_reaction\": \"Touch\", \"stamina\": 1, " + Extra + "}";
}

std::string describe(const std::vector<DataProblem>& Problems) {
    std::string Text;
    for (const DataProblem& Problem : Problems) Text += Problem.format() + "\n";
    return Text;
}

} // namespace

TEST_CASE("checkData: the real data directory has no problems", "[combat][data_check]") {
    const std::vector<DataProblem> Problems = checkData(FIGHTER_DATA_DIR);
    INFO(describe(Problems));
    CHECK(Problems.empty());
}

TEST_CASE("checkData: a move names a missing move, clip or close clip", "[combat][data_check]") {
    const test::ScratchData Data("check_moves");
    Data.write("moves/bad_chain.json", moveJson(R"("clip": "jab", "chain_to": ["no_such_move"])"));
    Data.write("moves/bad_clip.json", moveJson(R"("clip": "no_such_clip", "close_clip": "no_close_clip", "close_range_m": 0.5)"));
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "moves/bad_chain.json", "field 'chain_to': there is no move 'no_such_move'"));
    CHECK(hasProblem(Problems, "moves/bad_clip.json", "field 'clip': clip 'no_such_clip'"));
    CHECK(hasProblem(Problems, "moves/bad_clip.json", "field 'close_clip': clip 'no_close_clip'"));
    CHECK(Problems.size() == 3);
}

TEST_CASE("checkData: a clip without an active phase or strikers cannot hit", "[combat][data_check]") {
    const test::ScratchData Data("check_clips");
    Data.replace("poses/jab.json", "\"active\": [0.16, 0.28]", "\"active\": [0.2, 0.2]");
    Data.replace("poses/cross.json", "\"strikers\"", "\"strikers_off\"");   // an unknown field: a loader error
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "moves/jab.json", "empty active phase"));
    CHECK(hasProblem(Problems, "moves/cross.json", "poses/cross.json"));
}

TEST_CASE("checkData: a weapon move needs an arm among the strikers", "[combat][data_check]") {
    const test::ScratchData Data("check_weapon_move");
    // body_kick strikes with a foot.
    Data.write("moves/weapon_kick.json", moveJson(R"("clip": "kick", "uses_weapon": true)"));
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "moves/weapon_kick.json", "field 'uses_weapon'"));
}

TEST_CASE("checkData: movesets with unknown moves, parents and pairs", "[combat][data_check]") {
    const test::ScratchData Data("check_sets");
    Data.write("movesets/ghost_moves.json", R"({"moves": {"Light": "no_such_move"}})");
    Data.write("movesets/orphan.json", R"({"inherit": "no_such_set"})");
    Data.write("movesets/loop_a.json", R"({"inherit": "loop_b"})");
    Data.write("movesets/loop_b.json", R"({"inherit": "loop_a"})");
    Data.write("movesets/bad_pair.json", R"({"pair": ["sword", "no_such_set"]})");
    Data.write("movesets/twin.json", R"({"pair": ["sword", "shield"]})");   // sword_shield is for the pair
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "movesets/ghost_moves.json", "there is no move 'no_such_move'"));
    CHECK(hasProblem(Problems, "movesets/orphan.json", "there is no moveset 'no_such_set'"));
    CHECK(hasProblem(Problems, "movesets/loop_a.json", "in a circle"));
    CHECK(hasProblem(Problems, "movesets/loop_b.json", "in a circle"));
    CHECK(hasProblem(Problems, "movesets/bad_pair.json", "there is no moveset 'no_such_set'"));
    CHECK(hasProblem(Problems, "movesets/twin.json", "same pair"));
}

TEST_CASE("checkData: two entries with one normalized input are a conflict", "[combat][data_check]") {
    const test::ScratchData Data("check_conflict");
    Data.write("movesets/conflict.json", R"({"moves": {"Light+Heavy": "jab", "Heavy+Light": "cross"}})");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "movesets/conflict.json", "the input is already given"));
}

TEST_CASE("checkData: the clips of a moveset's block must exist", "[combat][data_check]") {
    const test::ScratchData Data("check_block");
    Data.write("movesets/odd_block.json", R"({"block": {"clips": {"Mid": "no_such_block"}}})");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "movesets/odd_block.json", "field 'block.clips.Mid': clip 'no_such_block'"));
}

TEST_CASE("checkData: the stance clip of a moveset must exist", "[combat][data_check]") {
    const test::ScratchData Data("check_stance");
    Data.write("movesets/odd_stance.json", R"({"stance": "no_such_stance"})");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "movesets/odd_stance.json", "field 'stance': clip 'no_such_stance'"));
}

TEST_CASE("checkData: the clips of the state machine must exist", "[combat][data_check]") {
    const test::ScratchData Data("check_state_clips");
    std::filesystem::remove(Data.getDir() / "poses" / "stance.json");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "poses/stance.json", "a clip the state machine plays"));
}

TEST_CASE("checkData: only attacks move the pelvis, within range", "[combat][data_check][pelvis]") {
    const test::ScratchData Data("check_pelvis_track");
    const std::string Track = R"("pelvisX": [ { "t": 0, "x": 0 }, { "t": 0.2, "x": 0.1 } ],)";
    Data.replace("poses/flinch.json", R"("loop": false,)", R"("loop": false, )" + Track);
    Data.write("poses/odd_guard.json", R"({ "duration": 0.5, )" + Track +
                                           R"( "keys": [ { "t": 0, "pose": { "UpperArmL": 40 } } ] })");
    Data.write("movesets/odd_guard.json", R"({"block": {"clips": {"High": "odd_guard"}}})");
    Data.replace("poses/jab.json", R"("blendOut": 0.08,)",
                 R"("blendOut": 0.08, "pelvisX": [ { "t": 0, "x": 0 }, { "t": 0.2, "x": 3.0 } ],)");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "poses/flinch.json", "field 'pelvisX'"));
    CHECK(hasProblem(Problems, "movesets/odd_guard.json", "'odd_guard' has a pelvis track"));
    CHECK(hasProblem(Problems, "moves/jab.json", "pelvisX: x = 3 is out of"));
}

TEST_CASE("checkData: items and fighters", "[combat][data_check]") {
    const test::ScratchData Data("check_items");
    Data.write("items/extra.json", R"({"items": [
        {"id": "short_sword", "slot": "MainHand", "mass_kg": 1.0, "moveset": "sword"},
        {"id": "odd_blade", "slot": "MainHand", "mass_kg": 1.0, "moveset": "no_such_set"}]})");
    Data.write("fighters/lost.json", R"({"name": "Lost", "stats": {"strength": 10, "dexterity": 10,
        "constitution": 10}, "items": ["no_such_item"]})");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "items/weapons.json", "duplicate item id 'short_sword'"));
    CHECK(hasProblem(Problems, "items/extra.json", "item 'odd_blade': field 'moveset': there is no moveset"));
    CHECK(hasProblem(Problems, "fighters/lost.json", "no_such_item"));
}

TEST_CASE("checkData: collects the problems of every file, also broken ones", "[combat][data_check]") {
    const test::ScratchData Data("check_all");
    Data.write("moves/broken.json", "{ not json");
    Data.write("movesets/typo.json", R"({"moves": {}, "mvoes": {}})");
    Data.write("fighters/broken.json", R"({"name": "X"})");
    std::filesystem::remove(Data.getDir() / "reactions.json");
    const auto Problems = checkData(Data.getDir());
    INFO(describe(Problems));
    CHECK(hasProblem(Problems, "moves/broken.json", ""));
    CHECK(hasProblem(Problems, "movesets/typo.json", "unknown field 'mvoes'"));
    CHECK(hasProblem(Problems, "fighters/broken.json", ""));
    CHECK(hasProblem(Problems, "reactions.json", ""));
    CHECK(Problems.size() == 4);   // the broken move is a stub in the library: no follow-up problems
}

TEST_CASE("checkData: a missing directory is a problem, not a throw", "[combat][data_check]") {
    const auto Problems = checkData(std::filesystem::temp_directory_path() / "fighter_no_such_data_dir");
    CHECK_FALSE(Problems.empty());
}

TEST_CASE("MoveLibrary::findProblems lists every problem", "[combat][data_check]") {
    std::vector<MoveDef> Moves = {{.Id = "jab", .Clip = "jab"}};
    std::vector<MoveSet> Sets = {parseMoveSet(R"({"moves": {"Light": "ghost"}})", "unarmed"),
                                 parseMoveSet(R"({"inherit": "nowhere"})", "lost")};
    const MoveLibrary Library = MoveLibrary::buildUnchecked(Moves, Sets, InputRules::getDefaults());
    const auto Problems = Library.findProblems();
    REQUIRE(Problems.size() == 2);
    CHECK(Problems[0].File == "movesets/unarmed.json");
    CHECK(Problems[1].File == "movesets/lost.json");
    CHECK(Problems[1].format() == "movesets/lost.json: field 'inherit': there is no moveset 'nowhere'");
}
