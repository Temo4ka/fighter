#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>

#include "combat/move_measure.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Matchers::ContainsSubstring;

namespace {

MeasureRequest makeRequest(std::string MoveId, std::string WeaponId = {}, bool WithDummy = true) {
    return {.MoveId = std::move(MoveId), .WeaponId = std::move(WeaponId), .WithDummy = WithDummy,
            .DataDir = FIGHTER_DATA_DIR};
}

} // namespace

TEST_CASE("getForwardDistance: positive towards the opponent", "[combat][measure]") {
    CHECK(getForwardDistance({1.0f, 0.0f}, {1.5f, 2.0f}, true) == 0.5f);
    CHECK(getForwardDistance({1.0f, 0.0f}, {0.5f, 2.0f}, false) == 0.5f);
    CHECK(getForwardDistance({1.0f, 0.0f}, {0.5f, 2.0f}, true) == -0.5f);
}

TEST_CASE("getPartTip: the end of the axis further from the pelvis, plus the weapon", "[combat][measure]") {
    // An upright part 0.4 m long centered at (0, 1): its ends are at y 0.8 and 1.2.
    const PartTransform Upright = {.Part = BodyPart::ForearmL, .Position = {0.0f, 1.0f}, .Angle = 0.0f, .Size = {0.1f, 0.4f}};
    const Vec2 FromBelow = getPartTip(Upright, {0.0f, 0.0f}, 0.0f);
    CHECK(FromBelow.Y == 1.2f);
    const Vec2 FromAbove = getPartTip(Upright, {0.0f, 2.0f}, 0.0f);
    CHECK(FromAbove.Y == 0.8f);
    CHECK(getPartTip(Upright, {0.0f, 0.0f}, 0.5f).Y == 1.7f);
    // Turned a quarter: the long axis is horizontal.
    PartTransform Sideways = Upright;
    Sideways.Angle = std::numbers::pi_v<float> * 0.5f;
    const Vec2 Tip = getPartTip(Sideways, {5.0f, 1.0f}, 0.0f);
    CHECK(std::abs(Tip.X - -0.2f) < 1e-5f);   // rotated +y goes to -x: that end is the one further from x = 5
    CHECK(std::abs(Tip.Y - 1.0f) < 1e-5f);
}

TEST_CASE("measureMove: a jab has a startup, an active phase, a recovery and a reach", "[combat][measure]") {
    const MoveMeasure Result = measureMove(makeRequest("jab"));
    INFO(describeMeasure(Result));
    CHECK(Result.Problem.empty());
    CHECK(Result.Started);
    CHECK(Result.Finished);
    CHECK(Result.StartupSec > 0.0f);
    CHECK(Result.ActiveSec > 0.0f);
    CHECK(Result.RecoverySec > 0.0f);
    REQUIRE_FALSE(Result.Paths.empty());
    CHECK(Result.Paths.front().Part == BodyPart::ForearmL);
    CHECK(Result.getReachM() > 0.0f);
    // The path starts at the move's start and its phases come in order.
    const auto& Points = Result.Paths.front().Points;
    REQUIRE(Points.size() > 3);
    CHECK(Points.front().TimeSec == 0.0f);
    CHECK(Points.front().Phase == AttackPhase::Startup);
    CHECK(Points.back().Phase == AttackPhase::Recovery);
    // The phases add up to the points: one point per step of the move.
    CHECK(std::abs(Result.getTotalSec() - static_cast<float>(Points.size()) / 60.0f) < 0.02f);
}

TEST_CASE("measureMove: the jab at its working range hits the dummy", "[combat][measure]") {
    const MoveMeasure Result = measureMove(makeRequest("jab"));
    INFO(describeMeasure(Result));
    CHECK(Result.Hit);
    CHECK(Result.HitTimeSec >= Result.StartupSec - 0.05f);
    CHECK(Result.Damage > 0.0f);
}

TEST_CASE("measureMove: without a dummy nothing is hit and the reach is the same kind of number", "[combat][measure]") {
    const MoveMeasure Free = measureMove(makeRequest("jab", {}, false));
    INFO(describeMeasure(Free));
    CHECK(Free.Problem.empty());
    CHECK_FALSE(Free.Hit);
    CHECK(Free.getReachM() > 0.0f);
    CHECK(Free.ActiveSec > 0.0f);
}

TEST_CASE("measureMove: a kick reports one leg", "[combat][measure]") {
    const MoveMeasure Result = measureMove(makeRequest("body_kick", {}, false));
    INFO(describeMeasure(Result));
    CHECK(Result.Problem.empty());
    // The clip strikes with the shin and the foot of one leg; the other leg's paths are dropped.
    REQUIRE(Result.Paths.size() == 2);
    const bool Left = Result.Paths[0].Part == BodyPart::ShinL || Result.Paths[0].Part == BodyPart::FootL;
    for (const StrikerPath& Path : Result.Paths) {
        const bool IsLeftLeg = Path.Part == BodyPart::ShinL || Path.Part == BodyPart::FootL;
        const bool IsRightLeg = Path.Part == BodyPart::ShinR || Path.Part == BodyPart::FootR;
        CHECK((Left ? IsLeftLeg : IsRightLeg));
    }
    CHECK(Result.getReachM() > 0.0f);
}

TEST_CASE("measureMove: the same request gives the same numbers", "[combat][measure]") {
    const MoveMeasure First = measureMove(makeRequest("jab"));
    const MoveMeasure Second = measureMove(makeRequest("jab"));
    CHECK(First.StartupSec == Second.StartupSec);
    CHECK(First.ActiveSec == Second.ActiveSec);
    CHECK(First.RecoverySec == Second.RecoverySec);
    CHECK(First.getReachM() == Second.getReachM());
    CHECK(First.Hit == Second.Hit);
}

TEST_CASE("measureMove: the pelvis travel of a lunge, and the reach with it", "[combat][measure][lunge]") {
    const MoveMeasure Plain = measureMove(makeRequest("jab", {}, false));
    CHECK(Plain.PelvisForwardM < 0.005f);
    CHECK_THAT(describeMeasure(Plain), !ContainsSubstring("pelvis"));

    test::ScratchData Data("measure_lunge");
    Data.replace("poses/jab.json", R"("blendOut": 0.08,)",
                 R"("blendOut": 0.08, "pelvisX": [ { "t": 0, "x": 0 }, { "t": 0.2, "x": 0.25 }, { "t": 0.41, "x": 0.1 } ],)");
    MeasureRequest Request = makeRequest("jab", {}, false);
    Request.DataDir = Data.getDir();
    const MoveMeasure Lunge = measureMove(Request);
    INFO(describeMeasure(Lunge));
    CHECK(std::abs(Lunge.PelvisForwardM - 0.25f) < 0.01f);
    CHECK(std::abs(Lunge.PelvisEndM - 0.1f) < 0.02f);
    CHECK(Lunge.PelvisBackM == 0.0f);
    CHECK(Lunge.PelvisPath.size() > 3);
    CHECK_THAT(describeMeasure(Lunge), ContainsSubstring("pelvis +0.25 m"));
    // The reach is from where the pelvis stood when the move began.
    CHECK(Lunge.getReachM() > Plain.getReachM() + 0.15f);
}

TEST_CASE("measureMove: a longer weapon of the same moveset reaches farther", "[combat][measure]") {
    const test::ScratchData Data("measure_reach");
    Data.write("items/blades.json", R"({"items": [
        {"id": "stub_dagger", "slot": "MainHand", "mass_kg": 1.0, "moveset": "sword",
         "weapon": {"reach_m": 0.2, "speed_scale": 1.0, "power_scale": 1.0}},
        {"id": "stub_pike", "slot": "MainHand", "mass_kg": 1.0, "moveset": "sword",
         "weapon": {"reach_m": 0.9, "speed_scale": 1.0, "power_scale": 1.0}}]})");
    MeasureRequest Short = makeRequest("sword_slash", "stub_dagger", false);
    MeasureRequest Long = makeRequest("sword_slash", "stub_pike", false);
    Short.DataDir = Long.DataDir = Data.getDir();
    const MoveMeasure ShortResult = measureMove(Short);
    const MoveMeasure LongResult = measureMove(Long);
    INFO(describeMeasure(ShortResult) << " / " << describeMeasure(LongResult));
    CHECK(ShortResult.Problem.empty());
    CHECK(LongResult.Problem.empty());
    CHECK(ShortResult.getReachM() > 0.0f);
    CHECK(LongResult.getReachM() > ShortResult.getReachM());
}

TEST_CASE("prepareStand: the weapon is found from the move when none is given", "[combat][measure]") {
    const StandSetup Bare = prepareStand(makeRequest("jab"));
    CHECK(Bare.WeaponId.empty());
    CHECK(Bare.MoveSetId == "unarmed");
    CHECK(Bare.InputText == "Light");
    CHECK(Bare.Input.Light);
    CHECK(Bare.WeaponReachM == 0.0f);

    const StandSetup Sword = prepareStand(makeRequest("sword_slash"));
    CHECK(Sword.WeaponId == "short_sword");
    CHECK(Sword.MoveSetId == "sword");
    CHECK(Sword.WeaponReachM > 0.0f);
    CHECK(Sword.Config.Left.Loadout.findWeapon() != nullptr);

    // A shield is an off-hand item, but the stand holds it in the main hand.
    const StandSetup Shield = prepareStand(makeRequest("shield_bash", "wooden_shield"));
    CHECK(Shield.MoveSetId == "shield");

    // A direction in the input becomes a stick direction.
    const StandSetup Thrust = prepareStand(makeRequest("sword_thrust", "short_sword"));
    CHECK(Thrust.InputText == "Forward+Heavy");
    CHECK(Thrust.Input.MoveX > 0.0f);
    CHECK(Thrust.Input.Heavy);
    const StandSetup Low = prepareStand(makeRequest("sword_low_cut", "short_sword"));
    CHECK(Low.Input.Down);
}

TEST_CASE("prepareStand: the dummy stands at the middle of the move's working range", "[combat][measure]") {
    const StandSetup Setup = prepareStand(makeRequest("jab"));
    REQUIRE(Setup.Config.SpawnDistanceM.has_value());
    CHECK(*Setup.Config.SpawnDistanceM > 0.5f);
    CHECK(*Setup.Config.SpawnDistanceM < 1.2f);
    const StandSetup Far = prepareStand(makeRequest("jab", {}, false));
    CHECK(*Far.Config.SpawnDistanceM == Far.Settings.NoDummyDistanceM);
}

TEST_CASE("prepareStand: unknown or unusable requests are clear errors", "[combat][measure]") {
    CHECK_THROWS_WITH(prepareStand(makeRequest("no_such_move")), ContainsSubstring("unknown move 'no_such_move'"));
    CHECK_THROWS_WITH(prepareStand(makeRequest("jab", "no_such_item")), ContainsSubstring("unknown item 'no_such_item'"));
    CHECK_THROWS_WITH(prepareStand(makeRequest("jab", "iron_helmet")), ContainsSubstring("cannot be held"));
    CHECK_THROWS_WITH(prepareStand(makeRequest("hammer_bash", "short_sword")),
                      ContainsSubstring("not started by any input of the moveset 'sword'"));
}

TEST_CASE("describeMeasure: running, failed and finished", "[combat][measure]") {
    MoveMeasure Result;
    CHECK(describeMeasure(Result) == "running");
    Result.Problem = "it did not start";
    CHECK(describeMeasure(Result) == "failed: it did not start");
    Result.Problem.clear();
    Result.Finished = true;
    Result.StartupSec = 0.15f;
    Result.ActiveSec = 0.1f;
    Result.RecoverySec = 0.25f;
    CHECK_THAT(describeMeasure(Result), ContainsSubstring("startup 0.15 s, active 0.10 s, recovery 0.25 s"));
    CHECK_THAT(describeMeasure(Result), ContainsSubstring("no hit"));
    Result.Hit = true;
    Result.HitPart = BodyPart::Head;
    Result.HitTimeSec = 0.2f;
    CHECK_THAT(describeMeasure(Result), ContainsSubstring("hit Head at 0.20 s"));
}

TEST_CASE("MoveRun: an input that starts another move is reported", "[combat][measure]") {
    StandSetup Setup = prepareStand(makeRequest("jab", {}, false));
    Setup.MoveId = "cross";   // the input is still "Light", which starts the jab
    Battle Fight(Setup.Config);
    MoveRun Run(Setup);
    for (int Tick = 0; Tick < 200 && !Run.isFinished(); ++Tick) {
        Fight.update(Run.getAttackerCommands(), PlayerCommands{}, 1.0 / 60.0);
        Run.observe(Fight);
    }
    CHECK(Run.isFinished());
    CHECK_THAT(Run.getMeasure().Problem, ContainsSubstring("started 'jab' instead of 'cross'"));
}

TEST_CASE("MoveRun: an input that starts nothing times out", "[combat][measure]") {
    StandSetup Setup = prepareStand(makeRequest("jab", {}, false));
    Setup.Input = {};   // nothing pressed
    Battle Fight(Setup.Config);
    MoveRun Run(Setup);
    for (int Tick = 0; Tick < 300 && !Run.isFinished(); ++Tick) {
        Fight.update(Run.getAttackerCommands(), PlayerCommands{}, 1.0 / 60.0);
        Run.observe(Fight);
    }
    CHECK(Run.isFinished());
    CHECK_FALSE(Run.getMeasure().Started);
    CHECK_THAT(Run.getMeasure().Problem, ContainsSubstring("did not start a move"));
}

TEST_CASE("MoveRun: the run is over only after the repeat pause", "[combat][measure]") {
    StandSetup Setup = prepareStand(makeRequest("jab", {}, false));
    Battle Fight(Setup.Config);
    MoveRun Run(Setup);
    int FinishedAt = -1;
    int OverAt = -1;
    for (int Tick = 0; Tick < 600 && OverAt < 0; ++Tick) {
        Fight.update(Run.getAttackerCommands(), PlayerCommands{}, 1.0 / 60.0);
        Run.observe(Fight);
        if (FinishedAt < 0 && Run.isFinished()) FinishedAt = Tick;
        if (Run.isOver()) OverAt = Tick;
    }
    REQUIRE(FinishedAt >= 0);
    REQUIRE(OverAt >= 0);
    CHECK(OverAt > FinishedAt);
    CHECK(static_cast<float>(OverAt - FinishedAt) / 60.0f >= Setup.Settings.RepeatPauseSec - 0.05f);
}
