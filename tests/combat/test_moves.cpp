#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "combat/moves.hpp"

using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

MoveDef makeMove(std::string Id, MoveButton Button, std::string Weapon = {}) {
    return {.Id = std::move(Id), .Button = Button, .Clip = "clip", .Weapon = std::move(Weapon)};
}

} // namespace

TEST_CASE("Moves: parse reads every field", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(R"({"button": "HeavyPunch", "clip": "sword_slash", "weapon": "sword",
        "damage": 1.4, "min_reaction": "Flinch", "stamina": 14, "min_startup_sec": 0.3})", "sword_slash");
    CHECK(Move.Id == "sword_slash");
    CHECK(Move.Button == MoveButton::HeavyPunch);
    CHECK(Move.Clip == "sword_slash");
    CHECK(Move.Weapon == "sword");
    CHECK(Move.Damage == 1.4f);
    CHECK(Move.MinReaction == ReactionLevel::Flinch);
    CHECK(Move.Stamina == 14.0f);
    CHECK(Move.MinStartupSec == 0.3f);
}

TEST_CASE("Moves: weapon and min_reaction are optional", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(
        R"({"button": "Jab", "clip": "jab", "damage": 0.5, "stamina": 5, "min_startup_sec": 0.15})", "jab");
    CHECK(Move.Weapon.empty());
    CHECK(Move.MinReaction == ReactionLevel::None);
    CHECK(Move.CloseClip.empty());
    CHECK(Move.ChainTo.empty());
    CHECK(Move.getClip(0.0f) == "jab");
    CHECK_FALSE(Move.canChainTo(MoveButton::Jab));
}

TEST_CASE("Moves: close-range clip and chains", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(R"({"button": "Jab", "clip": "jab", "damage": 0.5, "stamina": 5,
        "min_startup_sec": 0.15, "close_clip": "jab_close", "close_range_m": 0.6,
        "chain_to": ["Jab", "HeavyPunch"]})", "jab");
    CHECK(Move.CloseClip == "jab_close");
    CHECK(Move.CloseRangeM == 0.6f);
    CHECK(Move.getClip(0.5f) == "jab_close");
    CHECK(Move.getClip(0.6f) == "jab");
    CHECK(Move.canChainTo(MoveButton::Jab));
    CHECK(Move.canChainTo(MoveButton::HeavyPunch));
    CHECK_FALSE(Move.canChainTo(MoveButton::BodyKick));

    const auto Parse = [](const std::string& Extra) {
        return parseMoveDef(R"({"button": "Jab", "clip": "c", "damage": 1, "stamina": 1, "min_startup_sec": 0.1)" +
                                Extra + "}",
                            "m");
    };
    CHECK_THROWS_WITH(Parse(R"(, "close_clip": "c2")"),
                      Equals("fields 'close_clip' and 'close_range_m' must be given together"));
    CHECK_THROWS_WITH(Parse(R"(, "close_clip": "c2", "close_range_m": -1)"),
                      Equals("field 'close_range_m': -1 must not be negative"));
    CHECK_THROWS_WITH(Parse(R"(, "chain_to": ["Uppercut"])"), ContainsSubstring("unknown button 'Uppercut'"));
    CHECK_THROWS_WITH(Parse(R"(, "chain_to": ["Jab", "Jab"])"), ContainsSubstring("listed twice"));
    CHECK_THROWS_WITH(Parse(R"(, "chain_to": "Jab")"), ContainsSubstring("must be a list"));
}

TEST_CASE("Moves: parse rejects bad fields", "[combat][moves]") {
    const auto Parse = [](const std::string& Text) { return parseMoveDef(Text, "m"); };
    CHECK_THROWS_WITH(Parse(R"({"button": "Uppercut", "clip": "c", "damage": 1, "stamina": 1,
        "min_startup_sec": 0.1})"), ContainsSubstring("unknown button 'Uppercut'"));
    CHECK_THROWS_WITH(Parse(R"({"button": "Jab", "clip": "c", "damage": -1, "stamina": 1, "min_startup_sec": 0.1})"),
                      Equals("field 'damage': -1 must not be negative"));
    CHECK_THROWS_WITH(Parse(R"({"button": "Jab", "clip": "c", "damage": 1, "stamina": 1})"),
                      Equals("missing field 'min_startup_sec'"));
    CHECK_THROWS_WITH(Parse(R"({"button": "Jab", "clip": "c", "damage": 1, "stamina": 1, "min_startup_sec": 0.1,
        "speed": 2})"), Equals("unknown field 'speed'"));
    CHECK_THROWS_WITH(Parse(R"({"button": "Jab", "clip": "c", "damage": 1, "stamina": 1, "min_startup_sec": 0.1,
        "min_reaction": "Knockdown"})"), ContainsSubstring("field 'min_reaction'"));
    CHECK_THROWS_WITH(Parse(R"({"button": "Jab", "clip": "", "damage": 1, "stamina": 1, "min_startup_sec": 0.1})"),
                      Equals("field 'clip': must not be empty"));
}

TEST_CASE("Moves: a weapon move replaces the unarmed one on its button", "[combat][moves]") {
    const std::vector Moves = {
        makeMove("jab", MoveButton::Jab),
        makeMove("heavy_punch", MoveButton::HeavyPunch),
        makeMove("sword_slash", MoveButton::HeavyPunch, "sword"),
    };
    CHECK(findMove(Moves, MoveButton::HeavyPunch, "")->Id == "heavy_punch");
    CHECK(findMove(Moves, MoveButton::HeavyPunch, "sword")->Id == "sword_slash");
    CHECK(findMove(Moves, MoveButton::HeavyPunch, "hammer")->Id == "heavy_punch");
    CHECK(findMove(Moves, MoveButton::Jab, "sword")->Id == "jab");   // no sword move on this button
    CHECK(findMove(Moves, MoveButton::LowKick, "") == nullptr);
}

TEST_CASE("Moves: the sample set has an unarmed move on every button", "[combat][moves][data]") {
    const std::vector<MoveDef> Moves = loadMoveSet(DataDir / "moves");
    for (size_t Index = 0; Index < MoveButtonCount; ++Index) {
        const auto Button = static_cast<MoveButton>(Index);
        const MoveDef* Move = findMove(Moves, Button, "");
        REQUIRE(Move != nullptr);
        CHECK(Move->Weapon.empty());
    }
    // Every sample weapon class has a move of its own.
    for (const char* Weapon : {"sword", "hammer"}) {
        CHECK(findMove(Moves, MoveButton::HeavyPunch, Weapon)->Weapon == Weapon);
    }
}

TEST_CASE("Moves: two unarmed moves on one button are an error", "[combat][moves]") {
    const auto Dir = std::filesystem::temp_directory_path() / "fighter_test_moves";
    std::filesystem::remove_all(Dir);
    std::filesystem::create_directories(Dir);
    for (const char* Name : {"a", "b"}) {
        std::ofstream(Dir / (std::string(Name) + ".json"))
            << R"({"button": "Jab", "clip": "jab", "damage": 1, "stamina": 1, "min_startup_sec": 0.1})";
    }
    CHECK_THROWS_WITH(loadMoveSet(Dir), ContainsSubstring("b.json: button Jab is already taken by 'a'"));
    std::filesystem::remove_all(Dir);
}
