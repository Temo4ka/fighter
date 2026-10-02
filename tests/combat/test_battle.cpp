#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "combat/battle.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

// Scenario tests of a fight without a window: scripted input -> expected
// state. They are the definition of done of the physics spike (phase 1):
// the active ragdoll stands, walks, strikes, and its hits reach combat.
//
// Jumping, crouching and blocking are not implemented in phase 1
// (PlayerCommands::Jump/Crouch/Block are ignored), so there are no tests for
// them; the phase 0 jump test was dropped together with the kinematic
// placeholder.

namespace {

constexpr double Dt = 1.0 / 60.0;
constexpr int TicksPerSecond = 60;
/// Ticks for the fighters to settle from the reference pose into the stance.
constexpr int SettleTicks = TicksPerSecond;

BattleConfig makeConfig() {
    BattleConfig Config;
    Config.DataDir = FIGHTER_DATA_DIR;
    return Config;
}

void run(Battle& Fight, const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, int Ticks) {
    for (int Tick = 0; Tick < Ticks; ++Tick) Fight.update(LeftCmd, RightCmd, Dt);
}

const PartTransform& getPart(const FighterView& View, BodyPart Part) { return View.Parts[static_cast<size_t>(Part)]; }

const FighterView& getLeft(const Battle& Fight) { return Fight.getSnapshot().Fighters[0]; }
const FighterView& getRight(const Battle& Fight) { return Fight.getSnapshot().Fighters[1]; }

/// Upright: the head well above the floor and the torso close to vertical.
bool isUpright(const FighterView& View) {
    constexpr float MinHeadHeight = 1.4f;   // m; standing it is about 1.6 m
    constexpr float MaxTorsoTilt = 0.6f;    // rad
    return getPart(View, BodyPart::Head).Position.Y > MinHeadHeight &&
           std::abs(getPart(View, BodyPart::Torso).Angle) < MaxTorsoTilt;
}

/// The far end of the forearm (the fist) of a fighter facing right, in world
/// coordinates. In the reference pose a forearm hangs down.
Vec2 getFist(const FighterView& View) {
    const PartTransform& Forearm = getPart(View, BodyPart::ForearmL);
    return Forearm.Position + rotate({0.0f, -Forearm.Size.Y * 0.5f}, Forearm.Angle);
}

/// Walks P1 towards P2 and jabs (or kicks) every \p Period ticks for
/// \p Ticks, collecting the hits.
std::vector<physics::HitEvent> attackDummy(Battle& Fight, bool Kick, int Period, int Ticks) {
    std::vector<physics::HitEvent> Hits;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        const bool Press = Tick % Period < 3;
        const PlayerCommands LeftCmd{.MoveX = 1.0f, .Punch = Press && !Kick, .Kick = Press && Kick};
        Fight.update(LeftCmd, {}, Dt);
        std::ranges::copy(Fight.getHits(), std::back_inserter(Hits));
    }
    return Hits;
}

/// A copy of the data directory that a test may edit; removed afterwards.
class ScratchData {
public:
    explicit ScratchData(const std::string& Name)
        : Dir(std::filesystem::temp_directory_path() / ("fighter_test_" + Name)) {
        std::filesystem::remove_all(Dir);
        std::filesystem::copy(FIGHTER_DATA_DIR, Dir, std::filesystem::copy_options::recursive);
    }
    ~ScratchData() {
        std::error_code Ignored;
        std::filesystem::remove_all(Dir, Ignored);
    }
    ScratchData(const ScratchData&) = delete;
    ScratchData& operator=(const ScratchData&) = delete;

    const std::filesystem::path& getDir() const { return Dir; }

    void write(const std::filesystem::path& File, const std::string& Text) const {
        std::ofstream(Dir / File, std::ios::binary | std::ios::trunc) << Text;
    }

private:
    std::filesystem::path Dir;
};

} // namespace

TEST_CASE("Battle: fighters start on opposite sides facing each other", "[combat]") {
    Battle Fight(makeConfig());
    const FighterView& Left = getLeft(Fight);
    const FighterView& Right = getRight(Fight);
    CHECK(Left.Position.X < Right.Position.X);
    CHECK(Left.FacingRight);
    CHECK_FALSE(Right.FacingRight);
    CHECK(Left.Hp == Left.MaxHp);
    // Every body part is published for the renderer.
    CHECK(Left.Parts.size() == BodyPartCount);
    CHECK(Right.Parts.size() == BodyPartCount);
    CHECK(isUpright(Left));
    CHECK(isUpright(Right));
}

TEST_CASE("Battle: an idle fighter stands upright for 60 s", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    const float StartX = getLeft(Fight).Position.X;

    bool AlwaysUpright = true;
    float LowestHead = 10.0f;
    for (int Tick = 0; Tick < 60 * TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        for (const auto& View : Fight.getSnapshot().Fighters) {
            AlwaysUpright = AlwaysUpright && isUpright(View);
            LowestHead = std::min(LowestHead, getPart(View, BodyPart::Head).Position.Y);
        }
    }
    CHECK(AlwaysUpright);
    CHECK(LowestHead > 1.5f);
    // Standing still means standing still: no creeping across the arena.
    CHECK(getLeft(Fight).Position.X == Approx(StartX).margin(0.05f));
    CHECK(Fight.getHits().empty());
}

TEST_CASE("Battle: holding a direction for 3 s walks more than 1 m", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    const float StartX = getLeft(Fight).Position.X;
    bool AlwaysUpright = true;

    SECTION("forward (right)") {
        // P2 walks away (backwards) so that P1 has room.
        for (int Tick = 0; Tick < 3 * TicksPerSecond; ++Tick) {
            Fight.update({.MoveX = 1.0f}, {.MoveX = 1.0f}, Dt);
            AlwaysUpright = AlwaysUpright && isUpright(getLeft(Fight));
        }
        CHECK(getLeft(Fight).Position.X - StartX > 1.0f);
    }
    SECTION("backward (left)") {
        for (int Tick = 0; Tick < 3 * TicksPerSecond; ++Tick) {
            Fight.update({.MoveX = -1.0f}, {}, Dt);
            AlwaysUpright = AlwaysUpright && isUpright(getLeft(Fight));
        }
        CHECK(StartX - getLeft(Fight).Position.X > 1.0f);
    }
    CHECK(AlwaysUpright);
}

TEST_CASE("Battle: releasing the key stops the fighter", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    run(Fight, {.MoveX = -1.0f}, {}, 2 * TicksPerSecond);
    run(Fight, {}, {}, TicksPerSecond);   // the last step finishes

    const float StoppedX = getLeft(Fight).Position.X;
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getLeft(Fight).Position.X == Approx(StoppedX).margin(0.05f));
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: walking stops at the arena wall", "[combat]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {}, 10 * TicksPerSecond);
    const FighterView& Left = getLeft(Fight);
    CHECK(Left.Position.X > -Fight.getConfig().Arena.HalfWidthM);   // stayed inside
    CHECK(Left.Position.X < -Fight.getConfig().Arena.HalfWidthM + 0.6f);   // and got there
    CHECK(isUpright(Left));
}

TEST_CASE("Battle: a jab moves the fist forward", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    // Reach: how far the fist is in front of the pelvis.
    const auto getReach = [&] {
        return getFist(getLeft(Fight)).X - getPart(getLeft(Fight), BodyPart::Pelvis).Position.X;
    };
    const float GuardReach = getReach();

    float LongestReach = GuardReach;
    Fight.update({.Punch = true}, {}, Dt);
    for (int Tick = 0; Tick < TicksPerSecond / 2; ++Tick) {
        Fight.update({}, {}, Dt);
        LongestReach = std::max(LongestReach, getReach());
    }
    CHECK(LongestReach - GuardReach > 0.15f);
    // The jab is over and the fist is back in the guard.
    run(Fight, {}, {}, TicksPerSecond / 2);
    CHECK(getReach() == Approx(GuardReach).margin(0.1f));
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a kick raises the front foot forward", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    const Vec2 Start = getPart(getLeft(Fight), BodyPart::FootL).Position;

    Vec2 Highest = Start;
    Fight.update({.Kick = true}, {}, Dt);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        const Vec2 Foot = getPart(getLeft(Fight), BodyPart::FootL).Position;
        if (Foot.Y > Highest.Y) Highest = Foot;
    }
    CHECK(Highest.Y > 0.5f);
    CHECK(Highest.X > Start.X + 0.2f);
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a jab at the dummy is a hit with an impulse", "[combat][dod]") {
    Battle Fight(makeConfig());
    const std::vector<physics::HitEvent> Hits = attackDummy(Fight, false, TicksPerSecond * 2 / 3, 4 * TicksPerSecond);

    REQUIRE_FALSE(Hits.empty());
    for (const auto& Hit : Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        CHECK(Hit.Attacker.Part == BodyPart::ForearmL);
        CHECK(Hit.Victim.Fighter == 1);
        CHECK(Hit.Impulse > 0.0f);
        CHECK(Hit.ApproachSpeed > 0.0f);
    }
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a kick pushes the dummy", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, SettleTicks);
    const float DummyStartX = getRight(Fight).Position.X;

    const std::vector<physics::HitEvent> Hits = attackDummy(Fight, true, 2 * TicksPerSecond, 4 * TicksPerSecond);
    REQUIRE_FALSE(Hits.empty());
    float Strongest = 0.0f;
    for (const auto& Hit : Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        CHECK((Hit.Attacker.Part == BodyPart::FootL || Hit.Attacker.Part == BodyPart::ShinL));
        Strongest = std::max(Strongest, Hit.Impulse);
    }
    // A kick lands harder than a jab and the dummy reacts, but stays up.
    CHECK(Strongest > 10.0f);
    CHECK(getRight(Fight).Position.X != DummyStartX);
    CHECK(isUpright(getRight(Fight)));
}

TEST_CASE("Battle: same input gives the same result", "[combat][dod]") {
    Battle First(makeConfig());
    Battle Second(makeConfig());
    size_t HitCount = 0;
    for (int Tick = 0; Tick < 8 * TicksPerSecond; ++Tick) {
        const PlayerCommands LeftCmd{
            .MoveX = (Tick / 90) % 4 == 3 ? -1.0f : 1.0f,
            .Punch = Tick % 37 == 0,
            .Kick = Tick % 113 == 0,
        };
        const PlayerCommands RightCmd{.MoveX = (Tick / 70) % 2 ? -1.0f : 0.0f, .Punch = Tick % 53 == 0};
        First.update(LeftCmd, RightCmd, Dt);
        Second.update(LeftCmd, RightCmd, Dt);
        REQUIRE(First.getHits().size() == Second.getHits().size());
        HitCount += First.getHits().size();
    }
    for (auto&& [Lhs, Rhs] : std::views::zip(First.getSnapshot().Fighters, Second.getSnapshot().Fighters)) {
        CHECK(Lhs.Position == Rhs.Position);
        for (auto&& [PartL, PartR] : std::views::zip(Lhs.Parts, Rhs.Parts)) {
            CHECK(PartL.Position == PartR.Position);
            CHECK(PartL.Angle == PartR.Angle);
        }
    }
    // The scenario is only meaningful if the fighters actually hit each other.
    CHECK(HitCount > 0);
}

TEST_CASE("Battle: tuning is read from the data directory", "[combat][dod]") {
    ScratchData Data("tuning");
    BattleConfig Config = makeConfig();
    Config.DataDir = Data.getDir();

    SECTION("an edited value takes effect in a new battle") {
        const Battle Original(Config);
        const float DefaultGap = getRight(Original).Position.X - getLeft(Original).Position.X;
        Data.write("combat.json", R"({ "spawnDistance": 3.4, "hitSpeedThreshold": 0.6 })");
        const Battle Edited(Config);
        CHECK(getRight(Edited).Position.X - getLeft(Edited).Position.X == Approx(DefaultGap + 1.0f).margin(0.01f));
    }
    SECTION("a broken file is reported, not ignored") {
        Data.write("combat.json", "{ \"spawnDistance\": ");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
    SECTION("a misspelled key is reported") {
        Data.write("combat.json", R"({ "spawnDistanse": 3.0 })");
        CHECK_THROWS_AS(Battle(Config), std::runtime_error);
    }
}

TEST_CASE("Battle: result is available when round time runs out", "[combat]") {
    BattleConfig Config = makeConfig();
    Config.RoundTimeSec = 1.0;
    Battle Fight(Config);
    CHECK_FALSE(Fight.getResult().has_value());
    run(Fight, {}, {}, TicksPerSecond + 1);
    REQUIRE(Fight.getResult().has_value());
    CHECK(Fight.getResult()->TimeSec == Approx(1.0).margin(Dt));
}
