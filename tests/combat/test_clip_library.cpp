#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <stdexcept>
#include <vector>

#include "anim/layers.hpp"
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
    const std::vector<MoveDef> Moves = loadMoves(DataDir / "moves");
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
    CHECK_THROWS_WITH(ClipLibrary::load(Data.getDir() / "poses", std::span<const MoveDef>{}), ContainsSubstring("block_high.json"));

    // A move's clip is required too.
    const test::ScratchData Moves("clip_library_moves");
    std::filesystem::remove(Moves.getDir() / "poses" / "jab_close.json");
    CHECK_THROWS_WITH(ClipLibrary::load(Moves.getDir() / "poses", loadMoves(Moves.getDir() / "moves")),
                      ContainsSubstring("jab_close.json"));
}

TEST_CASE("ClipLibrary: the clips that pose the legs have a mirrored copy", "[combat][clips][data]") {
    const std::vector<MoveDef> Moves = loadMoves(DataDir / "moves");
    const ClipLibrary Clips = ClipLibrary::load(DataDir / "poses", Moves);
    for (const char* Name : {"kick", "low_kick", "crouch", "crouch_walk", "block_low"}) {
        INFO(Name);
        const anim::Clip& Authored = Clips.get(Name);
        const anim::Clip& Mirrored = Clips.getMirrored(Authored);
        CHECK(&Mirrored != &Authored);
        CHECK(Clips.isMirrored(Mirrored));
        CHECK_FALSE(Clips.isMirrored(Authored));
        CHECK(&Clips.getAuthored(Mirrored) == &Authored);
        CHECK(&Clips.getAuthored(Authored) == &Authored);
        CHECK(Mirrored.ActiveBeginSec == Authored.ActiveBeginSec);
    }
    // The kick strikes with the right leg when mirrored.
    CHECK(Clips.getMirrored(Clips.get("kick")).isStriker(BodyPart::FootR));
    CHECK_FALSE(Clips.getMirrored(Clips.get("kick")).isStriker(BodyPart::FootL));
    // A punch has no legs to mirror: it is its own copy.
    const anim::Clip& Jab = Clips.get("jab");
    CHECK(&Clips.getMirrored(Jab) == &Jab);
    CHECK_FALSE(Clips.isMirrored(Jab));
}

TEST_CASE("ClipLibrary: loads the block clips of the movesets", "[combat][clips]") {
    const test::ScratchData Data("clip_library_blocks");
    Data.write("movesets/sword.json", R"({"inherit": "unarmed", "moves": {"Heavy": "sword_slash"},
        "block": {"clips": {"Mid": "my_guard"}}})");
    std::filesystem::copy_file(Data.getDir() / "poses" / "block_mid.json", Data.getDir() / "poses" / "my_guard.json");
    const MoveLibrary Library = MoveLibrary::load(Data.getDir());
    const ClipLibrary Clips = ClipLibrary::load(Data.getDir() / "poses", Library);
    CHECK(Clips.find("my_guard") != nullptr);

    std::filesystem::remove(Data.getDir() / "poses" / "my_guard.json");
    CHECK_THROWS_WITH(ClipLibrary::load(Data.getDir() / "poses", Library),
                      ContainsSubstring("movesets/sword.json") && ContainsSubstring("my_guard"));
}

TEST_CASE("ClipLibrary: a clip played with the other hand", "[combat][clips][data]") {
    const ClipLibrary Clips = ClipLibrary::load(DataDir / "poses", MoveLibrary::load(DataDir));
    const anim::Clip& Slash = Clips.get("sword_slash");
    const anim::Clip& Left = Clips.getOtherHand(Slash);
    REQUIRE(&Left != &Slash);
    CHECK(Clips.isOtherHand(Left));
    CHECK_FALSE(Clips.isOtherHand(Slash));
    CHECK(&Clips.getAuthored(Left) == &Slash);
    CHECK(Left.isStriker(BodyPart::ForearmL));
    // A kick that poses the arms too has a copy with both the legs mirrored
    // and the arms swapped; one that does not is its own.
    const anim::Clip& Kick = Clips.get("kick");
    const anim::Clip& Mirrored = Clips.getMirrored(Kick);
    const anim::Clip& Both = Clips.getOtherHand(Mirrored);
    if (anim::usesArms(Kick)) {
        CHECK(&Both != &Mirrored);
        CHECK(&Clips.getAuthored(Both) == &Kick);
        CHECK(Both.isStriker(BodyPart::FootR));
    } else {
        CHECK(&Both == &Mirrored);
    }
}
