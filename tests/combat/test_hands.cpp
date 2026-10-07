#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "scenario.hpp"
#include "stats/equipment.hpp"
#include "stats/loading.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;

namespace {

/// The first move fighter \p Index starts while \p Inputs play one per tick
/// (the last one repeats up to \p Ticks), and the tick it started at.
struct FirstStart {
    std::string MoveId;
    int Tick = -1;
};

FirstStart findFirstStart(Battle& Fight, std::span<const PlayerCommands> Inputs, int Ticks, uint8_t Index = 0) {
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        const PlayerCommands& Cmd = Inputs[std::min(static_cast<size_t>(Tick), Inputs.size() - 1)];
        if (Index == 0) {
            Fight.update(Cmd, {}, Dt);
        } else {
            Fight.update({}, Cmd, Dt);
        }
        for (const BattleEvent& Event : Fight.getEvents()) {
            const auto* Start = std::get_if<StrikeStarted>(&Event);
            if (Start && Start->Fighter == Index) return {.MoveId = Start->MoveId, .Tick = Tick};
        }
    }
    return {};
}

} // namespace

TEST_CASE("Hands: buttons pressed within the combo window start the combination", "[combat][hands]") {
    ScratchData Data("combo_window");
    Data.replace("movesets/unarmed.json", "\"Light\": \"jab\",", "\"Light\": \"jab\", \"Light+Heavy\": \"body_kick\",");

    // Light, then Heavy two ticks later (within 0.05 s): the combination.
    {
        Battle Fight(Data.makeConfig());
        const std::vector<PlayerCommands> Inputs = {{.Light = true}, {.Light = true},
                                                    {.Light = true, .Heavy = true}};
        CHECK(findFirstStart(Fight, Inputs, 10).MoveId == "body_kick");
    }
    // Light alone: the jab, once the window is over.
    {
        Battle Fight(Data.makeConfig());
        const std::vector<PlayerCommands> Inputs = {{.Light = true}};
        const FirstStart Start = findFirstStart(Fight, Inputs, 10);
        CHECK(Start.MoveId == "jab");
        CHECK(Start.Tick >= 2);
    }
    // A set without combinations does not wait: the jab starts at once.
    {
        Battle Fight(makeConfig());
        const std::vector<PlayerCommands> Inputs = {{.Light = true}};
        const FirstStart Start = findFirstStart(Fight, Inputs, 10);
        CHECK(Start.MoveId == "jab");
        CHECK(Start.Tick == 0);
    }
}
