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

} // namespace

TEST_CASE("Moves: parse reads every field", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(R"({"clip": "sword_slash", "uses_weapon": true, "damage": 1.4,
        "min_reaction": "Flinch", "stamina": 14, "tags": ["mid", "swing"],
        "ai": {"range_m": [0.7, 1.1], "role": "heavy", "weight": 2}})", "sword_slash");
    CHECK(Move.Id == "sword_slash");
    CHECK(Move.Clip == "sword_slash");
    CHECK(Move.UsesWeapon);
    CHECK(Move.Damage == 1.4f);
    CHECK(Move.MinReaction == ReactionLevel::Flinch);
    CHECK(Move.Stamina == 14.0f);
    CHECK(Move.hasTag("swing"));
    CHECK(Move.getHeight() == "mid");
    REQUIRE(Move.Intent.has_value());
    CHECK(Move.Intent->MinRangeM == 0.7f);
    CHECK(Move.Intent->MaxRangeM == 1.1f);
    CHECK(Move.Intent->Role == "heavy");
    CHECK(Move.Intent->Weight == 2.0f);
}

TEST_CASE("Moves: optional fields keep their defaults", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(R"({"clip": "jab", "damage": 0.5, "stamina": 5})", "jab");
    CHECK_FALSE(Move.UsesWeapon);
    CHECK(Move.MinReaction == ReactionLevel::None);
    CHECK(Move.CloseClip.empty());
    CHECK(Move.ChainTo.empty());
    CHECK(Move.Tags.empty());
    CHECK(Move.getHeight().empty());
    CHECK_FALSE(Move.Intent.has_value());
    CHECK(Move.getClip(0.0f) == "jab");
    CHECK_FALSE(Move.canChainTo("jab"));
}

TEST_CASE("Moves: close-range clip and chains", "[combat][moves]") {
    const MoveDef Move = parseMoveDef(R"({"clip": "jab", "damage": 0.5, "stamina": 5,
        "close_clip": "jab_close", "close_range_m": 0.6,
        "chain_to": ["jab", "heavy_punch"]})", "jab");
    CHECK(Move.CloseClip == "jab_close");
    CHECK(Move.CloseRangeM == 0.6f);
    CHECK(Move.getClip(0.5f) == "jab_close");
    CHECK(Move.getClip(0.6f) == "jab");
    CHECK(Move.canChainTo("jab"));
    CHECK(Move.canChainTo("heavy_punch"));
    CHECK_FALSE(Move.canChainTo("body_kick"));

    const auto Parse = [](const std::string& Extra) {
        return parseMoveDef(R"({"clip": "c", "damage": 1, "stamina": 1)" + Extra + "}", "m");
    };
    CHECK_THROWS_WITH(Parse(R"(, "close_clip": "c2")"),
                      Equals("fields 'close_clip' and 'close_range_m' must be given together"));
    CHECK_THROWS_WITH(Parse(R"(, "close_clip": "c2", "close_range_m": -1)"),
                      Equals("field 'close_range_m': -1 must not be negative"));
    CHECK_THROWS_WITH(Parse(R"(, "chain_to": ["jab", "jab"])"), ContainsSubstring("listed twice"));
    CHECK_THROWS_WITH(Parse(R"(, "chain_to": "jab")"), ContainsSubstring("must be a list"));
}

TEST_CASE("Moves: parse rejects bad fields", "[combat][moves]") {
    const auto Parse = [](const std::string& Text) { return parseMoveDef(Text, "m"); };
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": -1, "stamina": 1})"),
                      Equals("field 'damage': -1 must not be negative"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1})"), Equals("missing field 'stamina'"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1, "stamina": 1, "button": "Jab"})"),
                      Equals("unknown field 'button'"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1, "stamina": 1, "min_reaction": "Knockdown"})"),
                      ContainsSubstring("field 'min_reaction'"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "", "damage": 1, "stamina": 1})"), Equals("field 'clip': must not be empty"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1, "stamina": 1, "tags": ["high", "low"]})"),
                      ContainsSubstring("more than one height"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1, "stamina": 1, "ai": {"range_m": [1, 0.5]}})"),
                      ContainsSubstring("field 'ai.range_m'"));
    CHECK_THROWS_WITH(Parse(R"({"clip": "c", "damage": 1, "stamina": 1, "ai": {"mood": "angry"}})"),
                      ContainsSubstring("field 'ai.mood': unknown field"));
}

TEST_CASE("Moves: a chain to a missing move is an error", "[combat][moves]") {
    const auto Dir = std::filesystem::temp_directory_path() / "fighter_test_moves";
    std::filesystem::remove_all(Dir);
    std::filesystem::create_directories(Dir);
    std::ofstream(Dir / "a.json") << R"({"clip": "jab", "damage": 1, "stamina": 1, "chain_to": ["b"]})";
    CHECK_THROWS_WITH(loadMoves(Dir), ContainsSubstring("a.json: field 'chain_to': there is no move 'b'"));
    std::ofstream(Dir / "b.json") << R"({"clip": "jab", "damage": 1, "stamina": 1})";
    const std::vector<MoveDef> Moves = loadMoves(Dir);
    CHECK(findMoveById(Moves, "b") != nullptr);
    CHECK(findMoveById(Moves, "c") == nullptr);
    std::filesystem::remove_all(Dir);
}

TEST_CASE("Moves: every sample move has a height tag", "[combat][moves][data]") {
    for (const MoveDef& Move : loadMoves(DataDir / "moves")) {
        INFO(Move.Id);
        CHECK_FALSE(Move.getHeight().empty());
    }
}
