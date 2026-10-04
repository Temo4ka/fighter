#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <stdexcept>
#include <vector>

#include "combat/clip_library.hpp"
#include "combat/moves.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

} // namespace

TEST_CASE("ClipLibrary: loads the state machine clips and every move clip", "[combat][clips][data]") {
    const std::vector<MoveDef> Moves = loadMoveSet(DataDir / "moves");
    const ClipLibrary Clips = ClipLibrary::load(DataDir / "poses", Moves);
    for (const MoveDef& Move : Moves) {
        CHECK(Clips.get(Move.Clip).Name == Move.Clip);
        if (!Move.CloseClip.empty()) CHECK(Clips.get(Move.CloseClip).Name == Move.CloseClip);
    }
    CHECK(Clips.get(clips::Stance).Name == "stance");
    CHECK(Clips.getBlock(BlockZone::High).Name == "block_high");
    CHECK(Clips.getBlock(BlockZone::Mid).Name == "block_mid");
    CHECK(Clips.getBlock(BlockZone::Low).Name == "block_low");
    CHECK(Clips.findReaction(ReactionLevel::Flinch)->Name == "flinch");
    CHECK(Clips.findReaction(ReactionLevel::Stagger)->Name == "stagger");
    CHECK(Clips.findReaction(ReactionLevel::Knockback)->Name == "knockback");
    // Touch interrupts nothing; a knockdown is the rig's ragdoll.
    CHECK(Clips.findReaction(ReactionLevel::Touch) == nullptr);
    CHECK(Clips.findReaction(ReactionLevel::Knockdown) == nullptr);
    CHECK_THROWS_AS(Clips.get("uppercut"), std::out_of_range);
}

TEST_CASE("ClipLibrary: a missing clip names its file", "[combat][clips]") {
    const test::ScratchData Data("clip_library");
    std::filesystem::remove(Data.getDir() / "poses" / "block_high.json");
    CHECK_THROWS_WITH(ClipLibrary::load(Data.getDir() / "poses", {}), ContainsSubstring("block_high.json"));

    // A move's clip is required too.
    const test::ScratchData Moves("clip_library_moves");
    std::filesystem::remove(Moves.getDir() / "poses" / "jab_close.json");
    CHECK_THROWS_WITH(ClipLibrary::load(Moves.getDir() / "poses", loadMoveSet(Moves.getDir() / "moves")),
                      ContainsSubstring("jab_close.json"));
}
