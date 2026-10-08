// Scenario tests of the pelvis track of a clip (anim::Clip::PelvisTrack): a
// lunge moves the body by the keyed distance in the open, stops at the
// opponent and at a wall without passing through, stops when interrupted,
// and the feet step with it instead of sliding.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "combat/battle.hpp"
#include "scenario.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

namespace {

/// A lunge of 0.25 m in the jab's startup that keys back to 0.1 m by its end.
constexpr const char* LungeTrack =
    R"([ { "t": 0.0, "x": 0.0 }, { "t": 0.2, "x": 0.25 }, { "t": 0.41, "x": 0.1 } ])";
/// A hop back of 0.4 m.
constexpr const char* HopBackTrack = R"([ { "t": 0.0, "x": 0.0 }, { "t": 0.3, "x": -0.4 } ])";

/// The scratch data with \p Track as the pelvis track of the jab.
void addJabTrack(const ScratchData& Data, const std::string& Track) {
    Data.replace("poses/jab.json", R"("blendOut": 0.08,)", std::string(R"("blendOut": 0.08, "pelvisX": )") + Track + ",");
}

/// What one run of the left fighter's jab did, step by step.
struct LungeRun {
    std::vector<float> PelvisX;     ///< P1's pelvis after each step.
    std::vector<float> FrontFootX;  ///< P1's FootL (in front in the stance).
    std::vector<float> RearFootX;   ///< P1's FootR.
    std::vector<float> FootReach;   ///< The farther foot from P1's pelvis, m.
    std::vector<FighterState> State;
    float WorstExcess = -1.0f;      ///< The worst overlap beyond its tolerance.
    std::vector<RenderSnapshot> Snapshots;
};

/// P1 jabs once (one tick of Light) and stands for \p Ticks; P2 does
/// \p RightCmd from \p RightFromTick on.
LungeRun runJab(Battle& Fight, int Ticks, PlayerCommands RightCmd = {}, int RightFromTick = 0) {
    LungeRun Run;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        const PlayerCommands Left = Tick == 0 ? press(MoveButton::Jab) : PlayerCommands{};
        Fight.update(Left, Tick >= RightFromTick ? RightCmd : PlayerCommands{}, Dt);
        const FighterView& View = getLeft(Fight);
        const Vec2 Pelvis = getPart(View, BodyPart::Pelvis).Position;
        Run.PelvisX.push_back(Pelvis.X);
        Run.FrontFootX.push_back(getPart(View, BodyPart::FootL).Position.X);
        Run.RearFootX.push_back(getPart(View, BodyPart::FootR).Position.X);
        Run.FootReach.push_back(std::max((getPart(View, BodyPart::FootL).Position - Pelvis).getLength(),
                                         (getPart(View, BodyPart::FootR).Position - Pelvis).getLength()));
        Run.State.push_back(View.State);
        if (const std::optional<physics::PartOverlap> Overlap = Fight.findWorstOverlap()) {
            Run.WorstExcess = std::max(Run.WorstExcess, Overlap->Depth - Fight.getOverlapTolerance(*Overlap));
        }
        Run.Snapshots.push_back(Fight.getSnapshot());
    }
    return Run;
}

/// The farthest the pelvis got forward (to the right) from \p StartX.
float getFarthest(const LungeRun& Run, float StartX) {
    return *std::ranges::max_element(Run.PelvisX) - StartX;
}

} // namespace

TEST_CASE("Lunge: the pelvis track moves the body by the keyed distance in the open", "[combat][lunge]") {
    ScratchData Data("lunge_open");
    addJabTrack(Data, LungeTrack);
    BattleConfig Config = Data.makeConfig();
    Config.SpawnDistanceM = 4.0f;
    Battle Fight(Config);
    run(Fight, {}, {}, 30);
    const float StartX = getPelvisX(getLeft(Fight));
    const float StanceReach = std::max((getPart(getLeft(Fight), BodyPart::FootL).Position -
                                        getPart(getLeft(Fight), BodyPart::Pelvis).Position).getLength(),
                                       (getPart(getLeft(Fight), BodyPart::FootR).Position -
                                        getPart(getLeft(Fight), BodyPart::Pelvis).Position).getLength());
    const float RearStartX = getPart(getLeft(Fight), BodyPart::FootR).Position.X;
    const float FrontStartX = getPart(getLeft(Fight), BodyPart::FootL).Position.X;

    const LungeRun Run = runJab(Fight, 90);
    INFO("farthest " << getFarthest(Run, StartX) << " m, end " << Run.PelvisX.back() - StartX << " m");
    // Out to the keyed 0.3 m and back to 0.1 m: the offset stays.
    CHECK(getFarthest(Run, StartX) == Approx(0.25f).margin(0.01f));
    CHECK(Run.PelvisX.back() - StartX == Approx(0.1f).margin(0.01f));

    // The rear foot stays planted while the pelvis goes out (its body turns
    // about the planted ankle a little); the front foot steps forward with it.
    const auto Out = static_cast<size_t>(std::ranges::max_element(Run.PelvisX) - Run.PelvisX.begin());
    for (size_t Tick = 0; Tick <= Out; ++Tick) CHECK(std::abs(Run.RearFootX[Tick] - RearStartX) < 0.02f);
    CHECK(Run.FrontFootX[Out] - FrontStartX > 0.1f);
    // The feet stay within the reach of the legs.
    const float WorstReach = *std::ranges::max_element(Run.FootReach);
    INFO("stance reach " << StanceReach << " m, worst " << WorstReach << " m");
    CHECK(WorstReach < StanceReach + 0.08f);

    // Afterwards the stance comes back by steps: the feet are as far apart
    // as in the stance and around the pelvis as in it.
    run(Fight, {}, {}, 90);
    const FighterView& After = getLeft(Fight);
    const float PelvisX = getPelvisX(After);
    CHECK(getPart(After, BodyPart::FootL).Position.X - PelvisX == Approx(FrontStartX - StartX).margin(0.13f));
    CHECK(getPart(After, BodyPart::FootR).Position.X - PelvisX == Approx(RearStartX - StartX).margin(0.13f));
}

TEST_CASE("Lunge: a lunge into the opponent stops without overlap", "[combat][lunge][overlap]") {
    ScratchData Data("lunge_opponent");
    addJabTrack(Data, LungeTrack);
    BattleConfig Config = Data.makeConfig();
    Config.SpawnDistanceM = 0.75f;
    Battle Fight(Config);
    run(Fight, {}, {}, 30);
    const float StartX = getPelvisX(getLeft(Fight));
    const float OpponentStartX = getPelvisX(getRight(Fight));

    const LungeRun Run = runJab(Fight, 90);
    const float Farthest = getFarthest(Run, StartX);
    INFO("farthest " << Farthest << " m, end " << Run.PelvisX.back() - StartX << " m, opponent moved "
                     << getPelvisX(getRight(Fight)) - OpponentStartX << " m, worst overlap excess "
                     << Run.WorstExcess << " m");
    // Held back short of the keyed 0.25 m; nothing passes through.
    CHECK(Farthest < 0.23f);
    CHECK(Run.WorstExcess <= 0.0f);
    // The rest of the track is lost: the keys back to 0.1 m (0.15 m back)
    // do not take the pelvis back; only the push-out of the jab that landed
    // at close range does, a little.
    CHECK(Run.PelvisX.back() - StartX > Farthest - 0.08f);
}

TEST_CASE("Lunge: a hop back stops at the wall", "[combat][lunge]") {
    ScratchData Data("lunge_wall");
    addJabTrack(Data, HopBackTrack);
    BattleConfig Config = Data.makeConfig();
    Config.SpawnDistanceM = 2.4f;
    // P1's pelvis starts 0.1 m from its wall limit.
    Config.Arena.HalfWidthM = 1.2f + 0.1f + 0.2f;
    Battle Fight(Config);
    run(Fight, {}, {}, 30);
    const float StartX = getPelvisX(getLeft(Fight));
    const LungeRun Run = runJab(Fight, 90);
    const float Back = StartX - *std::ranges::min_element(Run.PelvisX);
    INFO("start " << StartX << " m, went back " << Back << " m");
    CHECK(Back < 0.2f);
    CHECK(Back > 0.0f);
    CHECK(getLeft(Fight).AgainstWall);
}

TEST_CASE("Lunge: an interrupted lunge stops at once", "[combat][lunge]") {
    ScratchData Data("lunge_interrupted");
    // A slow lunge: 0.5 m over the whole jab.
    addJabTrack(Data, R"([ { "t": 0.0, "x": 0.0 }, { "t": 0.44, "x": 0.5 } ])");
    // Every touch of a strike staggers.
    ReactionSpec Spec;
    Spec.MinStrength = {0.01f, 0.02f, 0.03f, 900.0f, 1000.0f};
    Data.write("reactions.json", makeReactionsJson(Spec));
    BattleConfig Config = Data.makeConfig();
    Config.SpawnDistanceM = 1.4f;
    Battle Fight(Config);
    run(Fight, {}, {}, 30);

    // P2 kicks while P1 lunges in.
    PlayerCommands Kick = press(MoveButton::BodyKick);
    const LungeRun Run = runJab(Fight, 90, Kick, 1);
    const auto Hit = std::ranges::find(Run.State, FighterState::Reacting);
    REQUIRE(Hit != Run.State.end());
    const auto At = static_cast<size_t>(Hit - Run.State.begin());
    INFO("reacting from tick " << At);
    // The kick lands while the track still moves the pelvis (0.44 s).
    REQUIRE(At < 24);
    REQUIRE(Run.PelvisX[At - 1] > Run.PelvisX[At - 2]);
    // From the reaction on, the pelvis does not go on forward (the
    // knockback takes it back).
    for (size_t Tick = At + 1; Tick < Run.PelvisX.size() && Run.State[Tick] == FighterState::Reacting; ++Tick) {
        CHECK(Run.PelvisX[Tick] <= Run.PelvisX[Tick - 1] + 1e-4f);
    }
}

TEST_CASE("Lunge: the same lunge twice is the same", "[combat][lunge]") {
    ScratchData Data("lunge_determinism");
    addJabTrack(Data, LungeTrack);
    BattleConfig Config = Data.makeConfig();
    Config.SpawnDistanceM = 0.9f;
    const auto runOnce = [&] {
        Battle Fight(Config);
        run(Fight, {}, {}, 20);
        return runJab(Fight, 60);
    };
    const LungeRun First = runOnce();
    const LungeRun Second = runOnce();
    REQUIRE(First.Snapshots.size() == Second.Snapshots.size());
    for (size_t Tick = 0; Tick < First.Snapshots.size(); ++Tick) {
        const auto& A = First.Snapshots[Tick].Fighters;
        const auto& B = Second.Snapshots[Tick].Fighters;
        for (size_t Index = 0; Index < A.size(); ++Index) {
            for (size_t Part = 0; Part < A[Index].Parts.size(); ++Part) {
                REQUIRE(A[Index].Parts[Part].Position.X == B[Index].Parts[Part].Position.X);
                REQUIRE(A[Index].Parts[Part].Position.Y == B[Index].Parts[Part].Position.Y);
                REQUIRE(A[Index].Parts[Part].Angle == B[Index].Parts[Part].Angle);
            }
        }
    }
}
