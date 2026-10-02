#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "app/demo_script.hpp"

using namespace fighter::app;
using fighter::combat::RenderSnapshot;

namespace {

RenderSnapshot makeState(float Distance) {
    RenderSnapshot State;
    State.Fighters[0].Position = {-Distance * 0.5f, 0.0f};
    State.Fighters[1].Position = {Distance * 0.5f, 0.0f};
    return State;
}

} // namespace

TEST_CASE("findDemoScript: known names", "[app][demo]") {
    CHECK(findDemoScript("walk") == DemoScript::Walk);
    CHECK(findDemoScript("fight") == DemoScript::Fight);
    CHECK(findDemoScript("kick") == DemoScript::Kick);
    CHECK_FALSE(findDemoScript("dance").has_value());
}

TEST_CASE("getDemoInput: fight walks into range, then attacks", "[app][demo]") {
    const DemoInput Far = getDemoInput(DemoScript::Fight, 0, makeState(2.0f));
    CHECK(Far.Left.MoveX == 1.0f);
    CHECK_FALSE(Far.Left.Punch);
    // P2 is a dummy.
    CHECK(Far.Right == fighter::combat::PlayerCommands{});

    bool Punched = false;
    bool Kicked = false;
    for (uint64_t Tick = 0; Tick < 600; ++Tick) {
        const DemoInput Near = getDemoInput(DemoScript::Fight, Tick, makeState(0.5f));
        CHECK(Near.Left.MoveX == 0.0f);
        Punched = Punched || Near.Left.Punch;
        Kicked = Kicked || Near.Left.Kick;
    }
    CHECK(Punched);
    CHECK(Kicked);
}
