#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "ai/repertoire.hpp"
#include "combat/moveset.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

using namespace fighter;
using namespace fighter::ai;
using namespace fighter::combat;
using Catch::Approx;

namespace {

const AiMove* findAttack(const Repertoire& Moves, std::string_view MoveId) {
    const auto Found = std::ranges::find(Moves.Attacks, MoveId, &AiMove::MoveId);
    return Found == Moves.Attacks.end() ? nullptr : &*Found;
}

MoveLibrary makeLibrary() { return MoveLibrary::load(FIGHTER_DATA_DIR); }

} // namespace

TEST_CASE("buildRepertoire: the unarmed set gives its strikes with ranges", "[ai][repertoire][data]") {
    const MoveLibrary Library = makeLibrary();
    const Repertoire Moves = buildRepertoire(Library, *Library.findSet(UnarmedMoveSetId), AiConfig{});
    const AiMove* Jab = findAttack(Moves, "jab");
    REQUIRE(Jab);
    CHECK(Jab->Input.Buttons.contains(AttackButton::Light));
    CHECK(Jab->Input.Direction == InputDirection::Neutral);
    CHECK(Jab->MinRangeM == Approx(0.6f));
    CHECK(Jab->MaxRangeM == Approx(0.9f));
    CHECK(Jab->Role == "poke");
    // A move without a range takes the default one.
    const AiMove* Kick = findAttack(Moves, "body_kick");
    REQUIRE(Kick);
    CHECK(Kick->MinRangeM == Approx(AiConfig{}.DefaultMinRangeM));
    CHECK(Kick->MaxRangeM == Approx(AiConfig{}.DefaultMaxRangeM));
}

TEST_CASE("buildRepertoire: a child set overrides the input of its parent", "[ai][repertoire][data]") {
    const MoveLibrary Library = makeLibrary();
    const MoveSet* Sword = Library.findSet("sword");
    REQUIRE(Sword);
    const Repertoire Moves = buildRepertoire(Library, *Sword, AiConfig{});
    const AiMove* Heavy = nullptr;
    for (const AiMove& Attack : Moves.Attacks) {
        if (Attack.Input == MoveInput{InputDirection::Neutral, ButtonSet(AttackButton::Heavy)}) Heavy = &Attack;
    }
    REQUIRE(Heavy);
    CHECK(Heavy->MoveId == "sword_slash");
    // The parent's jab is still there on its own input.
    CHECK(findAttack(Moves, "jab"));
    // No two lines on one input.
    for (const AiMove& Attack : Moves.Attacks) {
        CHECK(std::ranges::count(Moves.Attacks, Attack.Input, &AiMove::Input) == 1);
    }
}

TEST_CASE("Repertoire::getHeight: the height tag of a move, mid if unknown", "[ai][repertoire][data]") {
    const MoveLibrary Library = makeLibrary();
    const Repertoire Moves = buildRepertoire(Library, *Library.findSet(UnarmedMoveSetId), AiConfig{});
    CHECK(Moves.getHeight("jab") == "high");
    CHECK(Moves.getHeight("body_kick") == "mid");
    CHECK(Moves.getHeight("no_such_move") == "mid");
    CHECK(Moves.getHeight("") == "mid");
}

TEST_CASE("selectMoveSet: an armed fighter gets the set of its weapon", "[ai][repertoire][data]") {
    const MoveLibrary Library = makeLibrary();
    const std::filesystem::path DataDir = FIGHTER_DATA_DIR;
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const stats::ResolvedFighter Knight =
        stats::resolveFighterSheet(stats::loadFighterSheet(DataDir / "fighters" / "knight.json"), Catalog);
    CHECK(selectMoveSet(Library, Knight.Gear).Id != UnarmedMoveSetId);
    CHECK(selectMoveSet(Library, stats::Loadout{}).Id == UnarmedMoveSetId);
}

TEST_CASE("makeAttackCommands: direction is relative to the facing", "[ai][repertoire]") {
    const MoveInput ForwardKick{InputDirection::Forward, ButtonSet(AttackButton::Kick)};
    const PlayerCommands Right = makeAttackCommands(ForwardKick, true);
    CHECK(Right.Kick);
    CHECK(Right.MoveX > 0.0f);
    CHECK_FALSE(Right.Light);
    CHECK(makeAttackCommands(ForwardKick, false).MoveX < 0.0f);

    const PlayerCommands UpLight = makeAttackCommands({InputDirection::Up, ButtonSet(AttackButton::Light)}, true);
    CHECK(UpLight.Up);
    CHECK(UpLight.Light);
    CHECK(UpLight.MoveX == 0.0f);

    const PlayerCommands DownBack = makeAttackCommands({InputDirection::DownBack, ButtonSet(AttackButton::Heavy)}, true);
    CHECK(DownBack.Down);
    CHECK(DownBack.MoveX < 0.0f);
    CHECK(DownBack.Heavy);
}

TEST_CASE("makeBlockCommands: the zone follows the height", "[ai][repertoire]") {
    CHECK(getBlockZone(makeBlockCommands("high"), true) == BlockZone::High);
    CHECK(getBlockZone(makeBlockCommands("mid"), true) == BlockZone::Mid);
    CHECK(getBlockZone(makeBlockCommands("low"), true) == BlockZone::Low);
    CHECK(getBlockZone(makeBlockCommands(""), false) == BlockZone::Mid);
    CHECK(getBlockZone(makeBlockCommands("high"), false) == BlockZone::High);
}
