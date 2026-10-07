#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "combat/moveset.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

MoveDef makeMove(std::string Id) { return {.Id = std::move(Id), .Clip = "clip"}; }

/// unarmed: Light jab, Kick body_kick, Down+Kick low_kick;
/// sword (inherits unarmed): Heavy slash, Forward+Heavy thrust, Light+Heavy spin;
/// sword_shield (pair sword + shield, inherits sword): Special bash.
MoveLibrary makeLibrary() {
    std::vector<MoveDef> Moves;
    for (const char* Id : {"jab", "body_kick", "low_kick", "slash", "thrust", "spin", "bash"}) {
        Moves.push_back(makeMove(Id));
    }
    std::vector<MoveSet> Sets = {
        parseMoveSet(R"({"moves": {"Light": "jab", "Kick": "body_kick", "Down+Kick": "low_kick"}})", "unarmed"),
        parseMoveSet(R"({"inherit": "unarmed",
            "moves": {"Heavy": "slash", "Forward+Heavy": "thrust", "Light+Heavy": "spin"},
            "block": {"damage_scale": 0.1, "clips": {"Mid": "block_mid_sword"}}})", "sword"),
        parseMoveSet(R"({"moves": {}})", "shield"),
        parseMoveSet(R"({"pair": ["sword", "shield"], "inherit": "sword", "moves": {"Special": "bash"},
            "block": {"max_level": "None", "covers": {"Mid": ["Head", "Torso"]}}})", "sword_shield"),
    };
    return MoveLibrary::build(std::move(Moves), std::move(Sets), InputRules::getDefaults());
}

const MoveDef* find(const MoveLibrary& Library, std::string_view SetId, InputDirection Direction, ButtonSet Held) {
    return Library.findMove(*Library.findSet(SetId), Direction, Held, Held);
}

std::string_view findId(const MoveLibrary& Library, std::string_view SetId, std::string_view Input) {
    const MoveInput Parsed = parseMoveInput(Input);
    const MoveDef* Move = find(Library, SetId, Parsed.Direction, Parsed.Buttons);
    return Move ? std::string_view(Move->Id) : std::string_view("-");
}

} // namespace

TEST_CASE("MoveSet: an input picks its move, then fallbacks, then the parent", "[combat][moveset]") {
    const MoveLibrary Library = makeLibrary();
    CHECK(findId(Library, "sword", "Heavy") == "slash");
    CHECK(findId(Library, "sword", "Forward+Heavy") == "thrust");
    CHECK(findId(Library, "sword", "Back+Heavy") == "slash");           // Back falls back to Neutral
    CHECK(findId(Library, "sword", "Kick") == "body_kick");             // from the parent
    CHECK(findId(Library, "sword", "DownForward+Kick") == "low_kick");  // DownForward -> Down, in the parent
    CHECK(findId(Library, "unarmed", "Heavy") == "-");
    CHECK(findId(Library, "unarmed", "Special") == "-");
}

TEST_CASE("MoveSet: buttons pressed together pick the combination", "[combat][moveset]") {
    const MoveLibrary Library = makeLibrary();
    const MoveSet& Sword = *Library.findSet("sword");
    ButtonSet Both(AttackButton::Light);
    Both.add(AttackButton::Heavy);
    CHECK(Library.findMove(Sword, InputDirection::Neutral, Both, Both)->Id == "spin");
    // Light held from before, Heavy pressed now: still the combination.
    CHECK(Library.findMove(Sword, InputDirection::Neutral, ButtonSet(AttackButton::Heavy), Both)->Id == "spin");
    // Only a pressed button starts a move: Kick held does not start a kick.
    ButtonSet LightAndKick(AttackButton::Light);
    LightAndKick.add(AttackButton::Kick);
    CHECK(Library.findMove(Sword, InputDirection::Neutral, ButtonSet(AttackButton::Light), LightAndKick)->Id ==
          "jab");
    const MoveSetEntry* Entry =
        Library.findEntry(Sword, InputDirection::DownBack, ButtonSet(AttackButton::Kick), ButtonSet(AttackButton::Kick));
    REQUIRE(Entry != nullptr);
    CHECK(Entry->Input.Direction == InputDirection::Down);
}

TEST_CASE("MoveSet: a pair set is chosen for exactly its pair", "[combat][moveset]") {
    const MoveLibrary Library = makeLibrary();
    CHECK(Library.selectSet("sword", "shield").Id == "sword_shield");
    CHECK(Library.selectSet("shield", "sword").Id == "shield");   // main hand first
    CHECK(Library.selectSet("sword", "").Id == "sword");
    CHECK(Library.selectSet("", "").Id == "unarmed");
    CHECK(Library.selectSet("axe", "").Id == "unarmed");
    CHECK(findId(Library, "sword_shield", "Special") == "bash");
    CHECK(findId(Library, "sword_shield", "Forward+Heavy") == "thrust");
}

TEST_CASE("MoveSet: the block inherits field by field", "[combat][moveset]") {
    const MoveLibrary Library = makeLibrary();
    BlockRules Defaults;
    Defaults.DamageScale = 0.2f;
    Defaults.MaxLevel = ReactionLevel::Touch;
    Defaults.Clips = {"block_high", "block_mid", "block_low"};
    Defaults.Covers[static_cast<size_t>(BlockZone::High)] = {BodyPart::Head};

    const BlockRules Unarmed = Library.getBlock(*Library.findSet("unarmed"), Defaults);
    CHECK(Unarmed.DamageScale == 0.2f);
    CHECK(Unarmed.getClip(BlockZone::Mid) == "block_mid");

    const BlockRules Pair = Library.getBlock(*Library.findSet("sword_shield"), Defaults);
    CHECK(Pair.DamageScale == 0.1f);                       // from sword
    CHECK(Pair.MaxLevel == ReactionLevel::None);           // its own
    CHECK(Pair.getClip(BlockZone::Mid) == "block_mid_sword");
    CHECK(Pair.getClip(BlockZone::High) == "block_high");  // the default
    CHECK(Pair.covers(BlockZone::Mid, BodyPart::Head));
    CHECK(Pair.covers(BlockZone::High, BodyPart::Head));
    CHECK_FALSE(Pair.covers(BlockZone::Mid, BodyPart::ForearmR));
}

TEST_CASE("MoveSet: parse rejects bad fields", "[combat][moveset]") {
    const auto Parse = [](const std::string& Text) { return parseMoveSet(Text, "s"); };
    CHECK_THROWS_WITH(Parse(R"({"moves": {"Jab": "jab"}})"), ContainsSubstring("field 'moves.Jab'"));
    CHECK_THROWS_WITH(Parse(R"({"moves": {"Forward+Heavy": "a", "Heavy+Forward": "b"}})"),
                      ContainsSubstring("already given as 'Forward+Heavy'"));
    CHECK_THROWS_WITH(Parse(R"({"pair": ["sword"]})"), ContainsSubstring("field 'pair'"));
    CHECK_THROWS_WITH(Parse(R"({"inherit": "s"})"), ContainsSubstring("cannot inherit itself"));
    CHECK_THROWS_WITH(Parse(R"({"block": {"damage_scale": 2}})"), ContainsSubstring("block.damage_scale"));
    CHECK_THROWS_WITH(Parse(R"({"block": {"clips": {"Top": "c"}}})"), ContainsSubstring("not High, Mid or Low"));
    CHECK_THROWS_WITH(Parse(R"({"block": {"covers": {"Mid": ["Tail"]}}})"), ContainsSubstring("unknown body part"));
    CHECK_THROWS_WITH(Parse(R"({"weapon": "sword"})"), Equals("unknown field 'weapon'"));
}

TEST_CASE("MoveLibrary: references are checked", "[combat][moveset]") {
    const auto Build = [](std::vector<std::string> Texts) {
        std::vector<MoveSet> Sets = {parseMoveSet(R"({"moves": {"Light": "jab"}})", "unarmed")};
        for (auto&& [Index, Text] : std::views::zip(std::views::iota(0), Texts)) {
            Sets.push_back(parseMoveSet(Text, "s" + std::to_string(Index)));
        }
        return MoveLibrary::build({makeMove("jab")}, std::move(Sets), InputRules::getDefaults());
    };
    CHECK_NOTHROW(Build({}));
    CHECK_THROWS_WITH(Build({R"({"moves": {"Heavy": "uppercut"}})"}),
                      Equals("movesets/s0.json: input 'Heavy': there is no move 'uppercut'"));
    CHECK_THROWS_WITH(Build({R"({"inherit": "axe"})"}), ContainsSubstring("there is no moveset 'axe'"));
    CHECK_THROWS_WITH(Build({R"({"inherit": "s1"})", R"({"inherit": "s0"})"}), ContainsSubstring("in a circle"));
    CHECK_THROWS_WITH(Build({R"({"pair": ["unarmed", "axe"]})"}), ContainsSubstring("there is no moveset 'axe'"));
    CHECK_THROWS_WITH(Build({R"({"pair": ["unarmed", "unarmed"]})", R"({"pair": ["unarmed", "unarmed"]})"}),
                      ContainsSubstring("for the same pair"));
    CHECK_THROWS_WITH(MoveLibrary::build({}, {}, {}), ContainsSubstring("there is no moveset 'unarmed'"));
}

TEST_CASE("MoveLibrary: the sample data loads", "[combat][moveset][data]") {
    const MoveLibrary Library = MoveLibrary::load(DataDir);
    const MoveSet& Unarmed = Library.selectSet("", "");
    const auto Find = [&](const MoveSet& Set, std::string_view Input) {
        const MoveInput Parsed = parseMoveInput(Input);
        const MoveDef* Move = Library.findMove(Set, Parsed.Direction, Parsed.Buttons, Parsed.Buttons);
        return Move ? Move->Id : std::string("-");
    };
    CHECK(Find(Unarmed, "Light") == "jab");
    CHECK(Find(Unarmed, "Heavy") == "heavy_punch");
    CHECK(Find(Unarmed, "Kick") == "body_kick");
    CHECK(Find(Unarmed, "Down+Kick") == "low_kick");
    CHECK(Find(Library.selectSet("sword", ""), "Heavy") == "sword_slash");
    CHECK(Find(Library.selectSet("hammer", ""), "Heavy") == "hammer_smash");
    CHECK(Find(Library.selectSet("hammer", ""), "Light") == "jab");
}
