#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "combat/tuning.hpp"
#include "rig/rig_def.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

using namespace fighter;
using namespace fighter::combat;
using Catch::Approx;

// Scenario tests of a fight without a window: scripted input -> expected
// state. They are the definition of done of the hybrid body (phase 1.5):
// the fighter stands and walks without balance assists, strikes land and
// sway the opponent, knockback depends on the mass, a strong hit knocks the
// fighter down and it gets up, and the simulation is deterministic.
//
// Crouching, blocking, the heavy punch and the low kick are not implemented
// yet (phase 2: the battle ignores those commands), so there are no tests
// for them.

namespace {

constexpr double Dt = 1.0 / 60.0;
constexpr int TicksPerSecond = 60;
/// Distance between the fighters' floor points at which the attacker stops
/// walking and strikes, m (as in the fight and kick demos).
constexpr float JabRange = 0.72f;
constexpr float KickRange = 0.95f;

BattleConfig makeConfig() {
    BattleConfig Config;
    Config.DataDir = FIGHTER_DATA_DIR;
    return Config;
}

rig::ControlParams loadControl() {
    return rig::loadRigDef(std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json").Control;
}

void run(Battle& Fight, const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, int Ticks) {
    for (int Tick = 0; Tick < Ticks; ++Tick) Fight.update(LeftCmd, RightCmd, Dt);
}

const PartTransform& getPart(const FighterView& View, BodyPart Part) { return View.Parts[static_cast<size_t>(Part)]; }

/// The strikes that landed during the last update().
std::vector<physics::HitEvent> getHits(const Battle& Fight) {
    std::vector<physics::HitEvent> Hits;
    for (const BattleEvent& Event : Fight.getEvents()) {
        if (const auto* Landed = std::get_if<StrikeLanded>(&Event)) Hits.push_back(Landed->Contact);
    }
    return Hits;
}

const FighterView& getLeft(const Battle& Fight) { return Fight.getSnapshot().Fighters[0]; }
const FighterView& getRight(const Battle& Fight) { return Fight.getSnapshot().Fighters[1]; }

float getPelvisX(const FighterView& View) { return getPart(View, BodyPart::Pelvis).Position.X; }
float getHeadHeight(const FighterView& View) { return getPart(View, BodyPart::Head).Position.Y; }

/// Upright: the head well above the floor and the torso close to vertical.
bool isUpright(const FighterView& View) {
    constexpr float MinHeadHeight = 1.4f;   // m; standing it is about 1.6 m
    constexpr float MaxTorsoTilt = 0.6f;    // rad
    return getHeadHeight(View) > MinHeadHeight && std::abs(getPart(View, BodyPart::Torso).Angle) < MaxTorsoTilt;
}

/// Down: the head close to the floor.
bool isDown(const FighterView& View) {
    constexpr float MaxHeadHeight = 0.7f;   // m
    return getHeadHeight(View) < MaxHeadHeight;
}

/// The far end of the forearm (the fist) of a fighter facing right, in world
/// coordinates. In the reference pose a forearm hangs down.
Vec2 getFist(const FighterView& View) {
    const PartTransform& Forearm = getPart(View, BodyPart::ForearmL);
    return Forearm.Position + rotate({0.0f, -Forearm.Size.Y * 0.5f}, Forearm.Angle);
}

/// The torso angle relative to the pelvis: how far the upper body leans.
float getTorsoLean(const FighterView& View) {
    return getPart(View, BodyPart::Torso).Angle - getPart(View, BodyPart::Pelvis).Angle;
}

/// What happened to the dummy (P2) while P1 attacked it.
struct AttackLog {
    std::vector<physics::HitEvent> Hits;
    std::optional<int> FirstHitTick;
    std::optional<int> DownTick;      ///< First tick with the dummy's head near the floor.
    float LargestSway = 0.0f;         ///< Largest change of the dummy's torso lean, rad.
};

/// P1 walks up to the dummy (P2) until \p Range, then jabs (or kicks) once
/// every \p Period ticks, for \p Ticks.
AttackLog attackDummy(Battle& Fight, bool Kick, int Period, int Ticks, float Range) {
    AttackLog Log;
    const float RestLean = getTorsoLean(getRight(Fight));
    int AttackTick = 0;
    for (int Tick = 0; Tick < Ticks; ++Tick) {
        PlayerCommands LeftCmd;
        if (getRight(Fight).Position.X - getLeft(Fight).Position.X > Range && AttackTick == 0) {
            LeftCmd.MoveX = 1.0f;
        } else {
            const bool Press = AttackTick++ % Period < 3;
            LeftCmd.Jab = Press && !Kick;
            LeftCmd.BodyKick = Press && Kick;
        }
        Fight.update(LeftCmd, {}, Dt);

        std::ranges::copy(getHits(Fight), std::back_inserter(Log.Hits));
        if (!Log.Hits.empty() && !Log.FirstHitTick) Log.FirstHitTick = Tick;
        if (isDown(getRight(Fight)) && !Log.DownTick) Log.DownTick = Tick;
        Log.LargestSway = std::max(Log.LargestSway, std::abs(getTorsoLean(getRight(Fight)) - RestLean));
    }
    return Log;
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

    /// Replaces the first \p From in \p File with \p To.
    void replace(const std::filesystem::path& File, const std::string& From, const std::string& To) const {
        std::stringstream Buffer;
        Buffer << std::ifstream(Dir / File, std::ios::binary).rdbuf();
        std::string Text = Buffer.str();
        const size_t At = Text.find(From);
        REQUIRE(At != std::string::npos);
        write(File, Text.replace(At, From.size(), To));
    }

private:
    std::filesystem::path Dir;
};

FighterConfig loadFighter(const std::string& Name) {
    const std::filesystem::path DataDir = FIGHTER_DATA_DIR;
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const stats::ResolvedFighter Sheet =
        stats::resolveFighterSheet(stats::loadFighterSheet(DataDir / "fighters" / (Name + ".json")), Catalog);
    FighterConfig Config;
    Config.Stats = Sheet.BaseStats;
    Config.Loadout = Sheet.Gear;
    return Config;
}

float getTotalMass(const FighterConfig& Config) {
    const stats::PhysicalProfile Profile =
        stats::computeProfile(Config.Stats, Config.Loadout, stats::BalanceTable::getDefaults());
    float Sum = 0.0f;
    for (const auto& Part : Profile.Parts) Sum += Part.MassKg;
    return Sum;
}

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
    // They start in the stance, standing on the floor.
    CHECK(isUpright(Left));
    CHECK(isUpright(Right));
    CHECK(Left.Position.Y == Approx(0.0f).margin(0.005f));
}

TEST_CASE("Battle: an idle fighter stands still for 60 s", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond);
    const float StartX = getPelvisX(getLeft(Fight));
    const float StartHead = getHeadHeight(getLeft(Fight));

    bool AlwaysUpright = true;
    float HeadDrift = 0.0f;
    for (int Tick = 0; Tick < 60 * TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        for (const auto& View : Fight.getSnapshot().Fighters) AlwaysUpright = AlwaysUpright && isUpright(View);
        HeadDrift = std::max(HeadDrift, std::abs(getHeadHeight(getLeft(Fight)) - StartHead));
    }
    CHECK(AlwaysUpright);
    // The pelvis is moved by the controller: standing still is exact.
    CHECK(getPelvisX(getLeft(Fight)) == StartX);
    CHECK(getLeft(Fight).Position.Y == Approx(0.0f).margin(0.005f));
    // The physical upper body settles and stays.
    CHECK(HeadDrift < 0.02f);
    CHECK(getHits(Fight).empty());
}

TEST_CASE("Battle: walking speed matches the data", "[combat][dod]") {
    const rig::ControlParams Control = loadControl();
    // P2 backs away so that P1 has room.
    const auto measureSpeed = [](Battle& Fight, float MoveX) {
        run(Fight, {.MoveX = MoveX}, {.MoveX = 1.0f}, TicksPerSecond / 2);   // accelerate
        const float StartX = getPelvisX(getLeft(Fight));
        run(Fight, {.MoveX = MoveX}, {.MoveX = 1.0f}, TicksPerSecond);
        return getPelvisX(getLeft(Fight)) - StartX;
    };

    SECTION("forward and backward") {
        Battle Forward(makeConfig());
        CHECK(measureSpeed(Forward, 1.0f) == Approx(Control.WalkSpeed).epsilon(0.01));
        CHECK(isUpright(getLeft(Forward)));
        Battle Backward(makeConfig());
        CHECK(measureSpeed(Backward, -1.0f) == Approx(-Control.WalkSpeed * Control.BackwardSpeedScale).epsilon(0.01));
    }
    SECTION("DEX makes a fighter faster") {
        BattleConfig Config = makeConfig();
        Config.Left.Stats.Dexterity = 20;
        const float Scale =
            stats::computeProfile(Config.Left.Stats, {}, stats::BalanceTable::getDefaults()).MoveSpeedScale;
        REQUIRE(Scale > 1.1f);
        Battle Nimble(Config);
        CHECK(measureSpeed(Nimble, 1.0f) == Approx(Control.WalkSpeed * Scale).epsilon(0.01));
    }
}

TEST_CASE("Battle: releasing the key stops the fighter", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {}, 2 * TicksPerSecond);
    run(Fight, {}, {}, TicksPerSecond / 2);   // decelerates, the step finishes

    const float StoppedX = getPelvisX(getLeft(Fight));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(getPelvisX(getLeft(Fight)) == StoppedX);
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: walking stops at the arena wall", "[combat]") {
    Battle Fight(makeConfig());
    run(Fight, {.MoveX = -1.0f}, {.MoveX = 1.0f}, 10 * TicksPerSecond);
    const float HalfWidth = Fight.getConfig().Arena.HalfWidthM;
    const float BodyHalfWidth = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json").BodyHalfWidth;
    CHECK(getPelvisX(getLeft(Fight)) == Approx(-HalfWidth + BodyHalfWidth));
    CHECK(getPelvisX(getRight(Fight)) == Approx(HalfWidth - BodyHalfWidth));
    for (const auto& View : Fight.getSnapshot().Fighters) {
        CHECK(std::abs(View.Position.X) < HalfWidth);
        CHECK(isUpright(View));
    }
}

TEST_CASE("Battle: fighters do not pass through each other", "[combat][dod]") {
    const float BodyHalfWidth = loadCombatTuning(std::filesystem::path(FIGHTER_DATA_DIR) / "combat.json").BodyHalfWidth;
    Battle Fight(makeConfig());
    float SmallestGap = 10.0f;
    for (int Tick = 0; Tick < 5 * TicksPerSecond; ++Tick) {
        Fight.update({.MoveX = 1.0f}, {.MoveX = -1.0f}, Dt);
        SmallestGap = std::min(SmallestGap, getPelvisX(getRight(Fight)) - getPelvisX(getLeft(Fight)));
    }
    CHECK(SmallestGap == Approx(2.0f * BodyHalfWidth).margin(1e-4f));

    // Walking into a standing fighter pushes it; the lighter one gives way more.
    BattleConfig Config = makeConfig();
    Config.Right.Stats.Constitution = 20;
    Battle Push(Config);
    const float HeavyStartX = getPelvisX(getRight(Push));
    run(Push, {.MoveX = 1.0f}, {}, 4 * TicksPerSecond);
    const float Pushed = getPelvisX(getRight(Push)) - HeavyStartX;
    CHECK(Pushed > 0.1f);
    CHECK(getPelvisX(getRight(Push)) - getPelvisX(getLeft(Push)) == Approx(2.0f * BodyHalfWidth).margin(1e-4f));
}

TEST_CASE("Battle: a jab moves the fist forward", "[combat][dod]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 2);
    // Reach: how far the fist is in front of the pelvis.
    const auto getReach = [&] { return getFist(getLeft(Fight)).X - getPelvisX(getLeft(Fight)); };
    const float GuardReach = getReach();

    float LongestReach = GuardReach;
    Fight.update({.Jab = true}, {}, Dt);
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
    const Vec2 Start = getPart(getLeft(Fight), BodyPart::FootL).Position;

    Vec2 Highest = Start;
    Fight.update({.BodyKick = true}, {}, Dt);
    for (int Tick = 0; Tick < TicksPerSecond; ++Tick) {
        Fight.update({}, {}, Dt);
        const Vec2 Foot = getPart(getLeft(Fight), BodyPart::FootL).Position;
        if (Foot.Y > Highest.Y) Highest = Foot;
    }
    CHECK(Highest.Y > 0.9f);   // body height: the torso is the physical target
    CHECK(Highest.X > Start.X + 0.3f);
    // The standing foot stays on the floor all along.
    CHECK(getLeft(Fight).Position.Y == Approx(0.0f).margin(0.005f));
    run(Fight, {}, {}, TicksPerSecond);
    CHECK(isUpright(getLeft(Fight)));
}

TEST_CASE("Battle: a jab at the dummy is a hit that sways it", "[combat][dod]") {
    Battle Fight(makeConfig());
    const AttackLog Log = attackDummy(Fight, false, TicksPerSecond * 2 / 3, 4 * TicksPerSecond, JabRange);

    REQUIRE_FALSE(Log.Hits.empty());
    for (const auto& Hit : Log.Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        CHECK(Hit.Attacker.Part == BodyPart::ForearmL);
        CHECK(Hit.Victim.Fighter == 1);
        CHECK(Hit.Impulse > 0.0f);
        CHECK(Hit.ApproachSpeed > 0.0f);
    }
    // The physical upper body of the dummy reacts, but a jab does not knock
    // it down.
    CHECK(Log.LargestSway > 0.05f);
    CHECK_FALSE(Log.DownTick.has_value());
    CHECK(isUpright(getLeft(Fight)));
    CHECK(isUpright(getRight(Fight)));
}

TEST_CASE("Battle: a kick at the dummy is a hit that sways and pushes it", "[combat][dod]") {
    // Without knockdowns, to see the plain reaction.
    ScratchData Data("kick");
    Data.replace("rigs/humanoid.json", "\"knockdownSpeed\": 0.7", "\"knockdownSpeed\": 100");
    BattleConfig Config = makeConfig();
    Config.DataDir = Data.getDir();
    Battle Fight(Config);
    const float DummyStartX = getPelvisX(getRight(Fight));

    const AttackLog Log = attackDummy(Fight, true, 2 * TicksPerSecond, 4 * TicksPerSecond, KickRange);
    REQUIRE_FALSE(Log.Hits.empty());
    float Strongest = 0.0f;
    for (const auto& Hit : Log.Hits) {
        CHECK(Hit.Attacker.Fighter == 0);
        CHECK((Hit.Attacker.Part == BodyPart::FootL || Hit.Attacker.Part == BodyPart::ShinL));
        Strongest = std::max(Strongest, Hit.Impulse);
    }
    // A kick lands harder than a jab; the dummy sways and is pushed back.
    CHECK(Strongest > 20.0f);
    CHECK(Log.LargestSway > 0.1f);
    CHECK(getPelvisX(getRight(Fight)) > DummyStartX + 0.05f);
    CHECK(isUpright(getRight(Fight)));
}

TEST_CASE("Battle: a heavy fighter is knocked back less than a light one", "[combat][dod]") {
    // Knockdowns off: both must stay standing for the knockback to compare.
    ScratchData Data("knockback");
    Data.replace("rigs/humanoid.json", "\"knockdownSpeed\": 0.7", "\"knockdownSpeed\": 100");
    const auto measureKnockback = [&](const FighterConfig& Dummy) {
        BattleConfig Config = makeConfig();
        Config.DataDir = Data.getDir();
        Config.Right = Dummy;
        Battle Fight(Config);
        // One kick; the dummy only moves when it is hit.
        const float StartX = getPelvisX(getRight(Fight));
        const AttackLog Log = attackDummy(Fight, true, 10 * TicksPerSecond, 3 * TicksPerSecond, KickRange);
        REQUIRE(Log.FirstHitTick.has_value());
        return getPelvisX(getRight(Fight)) - StartX;
    };

    // The knight: more CON and heavy armor; the rogue: less of both.
    const FighterConfig Knight = loadFighter("knight");
    const FighterConfig Rogue = loadFighter("rogue");
    REQUIRE(getTotalMass(Knight) > getTotalMass(Rogue) * 1.3f);
    const float HeavyDistance = measureKnockback(Knight);
    const float LightDistance = measureKnockback(Rogue);
    CHECK(LightDistance > 0.1f);
    CHECK(HeavyDistance > 0.0f);
    CHECK(HeavyDistance < LightDistance * 0.85f);

    // CON alone: the same body, only heavier.
    FighterConfig Tough;
    Tough.Stats.Constitution = 20;
    CHECK(measureKnockback(Tough) < measureKnockback(FighterConfig{}) * 0.9f);
}

TEST_CASE("Battle: a strong kick knocks the fighter down and it gets up", "[combat][dod]") {
    const rig::ControlParams Control = loadControl();
    Battle Fight(makeConfig());
    // One kick, then nothing: the dummy falls, lies and gets up.
    const int Ticks = 6 * TicksPerSecond;
    const AttackLog Log = attackDummy(Fight, true, Ticks, Ticks, KickRange);

    REQUIRE(Log.FirstHitTick.has_value());
    REQUIRE(Log.DownTick.has_value());
    CHECK(*Log.DownTick - *Log.FirstHitTick < TicksPerSecond);   // falls right away
    CHECK(isUpright(getRight(Fight)));
    CHECK(isUpright(getLeft(Fight)));

    // It is up again within the configured time on the floor and getting up
    // (plus a little for the upper body to straighten).
    Battle Again(makeConfig());
    std::optional<int> UpTick;
    std::optional<int> HitTick;
    for (int Tick = 0; Tick < Ticks && !UpTick; ++Tick) {
        const bool InRange = getRight(Again).Position.X - getLeft(Again).Position.X <= KickRange;
        Again.update({.MoveX = HitTick || InRange ? 0.0f : 1.0f, .BodyKick = InRange && !HitTick}, {}, Dt);
        if (!HitTick && !getHits(Again).empty()) HitTick = Tick;
        if (HitTick && Tick > *HitTick + TicksPerSecond && isUpright(getRight(Again))) UpTick = Tick;
    }
    REQUIRE(UpTick.has_value());
    const float UpSec = static_cast<float>(*UpTick - *HitTick) / TicksPerSecond;
    CHECK(UpSec > Control.KnockdownSec);
    CHECK(UpSec < Control.KnockdownSec + Control.GetUpSec + 0.5f);
    CHECK(getRight(Again).Position.Y == Approx(0.0f).margin(0.005f));   // on its feet
}

TEST_CASE("Battle: same input gives the same result", "[combat][dod]") {
    Battle First(makeConfig());
    Battle Second(makeConfig());
    size_t HitCount = 0;
    bool KnockedDown = false;
    for (int Tick = 0; Tick < 10 * TicksPerSecond; ++Tick) {
        // Both battles get the same input, computed from the first one.
        const float Distance = getRight(First).Position.X - getLeft(First).Position.X;
        // P1 walks into kicking range and kicks every 2.5 s, jabbing in between.
        const int Phase = Tick % 150;
        const PlayerCommands LeftCmd{
            .MoveX = Distance > KickRange ? 1.0f : 0.0f,
            .Jab = Phase > 40 && Phase < 120 && Tick % 37 == 0,
            .BodyKick = Distance <= KickRange && Phase < 3,
        };
        const PlayerCommands RightCmd{.MoveX = (Tick / 70) % 3 == 2 ? 1.0f : 0.0f, .Jab = Tick % 53 == 0};
        First.update(LeftCmd, RightCmd, Dt);
        Second.update(LeftCmd, RightCmd, Dt);
        REQUIRE(getHits(First).size() == getHits(Second).size());
        HitCount += getHits(First).size();
        KnockedDown = KnockedDown || isDown(getRight(First)) || isDown(getLeft(First));
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
    CHECK(KnockedDown);
}

TEST_CASE("Battle: tuning is read from the data directory", "[combat][dod]") {
    ScratchData Data("tuning");
    BattleConfig Config = makeConfig();
    Config.DataDir = Data.getDir();

    SECTION("an edited value takes effect in a new battle") {
        const Battle Original(Config);
        const float DefaultGap = getRight(Original).Position.X - getLeft(Original).Position.X;
        Data.replace("combat.json", "\"spawnDistance\": 2.4", "\"spawnDistance\": 3.4");
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

// Phase 2 contracts (tasks 2.0.1, 2.0.4): what the stub battle already
// reports through events, the snapshot and the result.

TEST_CASE("Battle: a knockdown kick is told by events and the result", "[combat][events]") {
    BattleConfig Config = makeConfig();
    Config.RoundTimeSec = 6.0;
    Battle Fight(Config);

    std::vector<BattleEvent> Log;
    bool Kicked = false;
    while (!Fight.getResult()) {
        const bool InRange = getRight(Fight).Position.X - getLeft(Fight).Position.X <= KickRange;
        Fight.update({.MoveX = Kicked || InRange ? 0.0f : 1.0f, .BodyKick = InRange && !Kicked}, {}, Dt);
        Kicked = Kicked || InRange;
        std::ranges::copy(Fight.getEvents(), std::back_inserter(Log));
    }

    // In order: the kick starts, lands, the dummy falls and gets up, the time runs out.
    const auto findEvent = [&]<class Event>(std::type_identity<Event>, size_t From) {
        for (size_t Index = From; Index < Log.size(); ++Index) {
            if (std::holds_alternative<Event>(Log[Index])) return Index;
        }
        return Log.size();
    };
    const size_t Started = findEvent(std::type_identity<StrikeStarted>{}, 0);
    const size_t Landed = findEvent(std::type_identity<StrikeLanded>{}, Started);
    const size_t Down = findEvent(std::type_identity<KnockedDown>{}, Landed);
    const size_t Up = findEvent(std::type_identity<GotUp>{}, Down);
    REQUIRE(Up < Log.size());
    CHECK(std::get<StrikeStarted>(Log[Started]).Fighter == 0);
    CHECK(std::get<StrikeStarted>(Log[Started]).MoveId == "body_kick");
    CHECK(std::get<StrikeLanded>(Log[Landed]).Contact.Victim.Fighter == 1);
    CHECK(std::get<KnockedDown>(Log[Down]).Fighter == 1);
    CHECK(std::get<GotUp>(Log[Up]).Fighter == 1);
    REQUIRE(std::holds_alternative<BattleOver>(Log.back()));
    CHECK(std::get<BattleOver>(Log.back()).End == BattleEnd::TimeUp);

    const BattleResult& Result = *Fight.getResult();
    CHECK(Result.End == BattleEnd::TimeUp);
    CHECK(Result.TimeSec == Approx(Config.RoundTimeSec).margin(Dt));
    CHECK(Result.WinnerSide == Winner::Draw);   // no damage yet
    const StrikeStats& Kick = Result.Fighters[0].Moves.at("body_kick");
    CHECK(Kick.Thrown == 1);
    CHECK(Kick.Landed == 1);
    uint32_t HitsTaken = 0;
    for (const PartReport& Part : Result.Fighters[1].HitsTaken) HitsTaken += Part.Hits;
    CHECK(HitsTaken == 1);
    CHECK(Result.Fighters[1].Knockdowns == 1);
    CHECK(Result.Fighters[0].Knockdowns == 0);
    CHECK(Result.Fighters[0].Hp == getLeft(Fight).Hp);

    // After the end nothing happens any more.
    Fight.update({}, {}, Dt);
    CHECK(Fight.getEvents().empty());
}

TEST_CASE("Battle: starting HP comes from the config", "[combat][config]") {
    BattleConfig Config = makeConfig();
    Config.Left.StartHp = 30.0f;
    Config.Right.StartHp = 1.0e6f;
    const Battle Fight(Config);
    CHECK(getLeft(Fight).Hp == 30.0f);
    CHECK(getRight(Fight).Hp == getRight(Fight).MaxHp);   // clamped
    CHECK(getRight(Fight).MaxHp > 30.0f);
}

TEST_CASE("Battle: the snapshot shows the phases of an attack", "[combat][snapshot]") {
    Battle Fight(makeConfig());
    run(Fight, {}, {}, TicksPerSecond / 4);
    CHECK(getLeft(Fight).State == FighterState::Idle);
    CHECK(getLeft(Fight).MoveId.empty());

    Fight.update({.Jab = true}, {}, Dt);
    std::vector<AttackPhase> Phases;
    for (int Tick = 0; Tick < TicksPerSecond && getLeft(Fight).State == FighterState::Attacking; ++Tick) {
        CHECK(getLeft(Fight).MoveId == "jab");
        if (Phases.empty() || Phases.back() != getLeft(Fight).Phase) Phases.push_back(getLeft(Fight).Phase);
        Fight.update({}, {}, Dt);
    }
    CHECK(Phases == std::vector{AttackPhase::Startup, AttackPhase::Active, AttackPhase::Recovery});
    CHECK(getLeft(Fight).State == FighterState::Idle);
    CHECK(getLeft(Fight).Phase == AttackPhase::None);

    Fight.update({.MoveX = 1.0f}, {}, Dt);
    CHECK(getLeft(Fight).State == FighterState::Walking);
}
