#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "combat/battle.hpp"
#include "combat/move_measure.hpp"
#include "combat/stand_config.hpp"
#include "scenario.hpp"
#include "stats/equipment.hpp"
#include "stats/loading.hpp"
#include "stats/stats.hpp"

using namespace fighter;
using namespace fighter::combat;
using namespace fighter::combat::test;
using Catch::Approx;

namespace {

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
const std::filesystem::path DataDir = FIGHTER_DATA_DIR;

/// A fighter with the short sword of the items in \p Dir.
FighterConfig makeSwordsman(const std::filesystem::path& Dir = DataDir) {
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(Dir / "items");
    FighterConfig Config;
    const std::vector<std::string> Items = {"short_sword"};
    Config.Loadout = stats::buildLoadout(Items, Catalog);
    return Config;
}

/// The wrist of the weapon \p Holder holds, as a pose writes it (for a
/// fighter facing right): the weapon's turn from the forearm.
float getWrist(const FighterView& View, BodyPart Holder) {
    for (const PartTransform& Weapon : View.Weapons) {
        if (Weapon.Part != Holder) continue;
        const float Turn = std::remainder(Weapon.Angle - getPart(View, Holder).Angle, 2.0f * std::numbers::pi_v<float>);
        return View.FacingRight ? Turn : -Turn;
    }
    FAIL("no weapon in that hand");
    return 0.0f;
}

/// The left fighter's wrist after standing still for a second.
float getIdleWrist(const BattleConfig& Base) {
    BattleConfig Config = Base;
    Config.Left = makeSwordsman(Config.DataDir);
    Battle Fight(Config);
    run(Fight, {}, {}, TicksPerSecond);
    return getWrist(getLeft(Fight), BodyPart::ForearmL);
}

std::string readText(const std::filesystem::path& File) {
    std::stringstream Buffer;
    Buffer << std::ifstream(File, std::ios::binary).rdbuf();
    return Buffer.str();
}

/// \p Text with \p Insert after every \p After.
std::string insertAfterEach(std::string Text, const std::string& After, const std::string& Insert) {
    for (size_t At = Text.find(After); At != std::string::npos; At = Text.find(After, At + After.size())) {
        Text.insert(At + After.size(), Insert);
    }
    return Text;
}

} // namespace

TEST_CASE("Wrist: missing in a clip, the stance's, else the item's, else the rig's default", "[combat][wrist][data]") {
    // The shipped data: no stance sets it and the sword has no angle.
    CHECK(getIdleWrist(makeConfig()) == Approx(0.0f).margin(0.05f));

    ScratchData RigDefault("wrist_rig");
    RigDefault.replace("rigs/humanoid.json", R"("angle": 0, "width")", R"("angle": 20, "width")");
    CHECK(getIdleWrist(RigDefault.makeConfig()) == Approx(20.0f * RadiansPerDegree).margin(0.05f));

    ScratchData ItemDefault("wrist_item");
    ItemDefault.replace("rigs/humanoid.json", R"("angle": 0, "width")", R"("angle": 20, "width")");
    ItemDefault.replace("items/weapons.json", R"("reach_m": 0.55,)", R"("reach_m": 0.55, "angle_deg": 40,)");
    CHECK(getIdleWrist(ItemDefault.makeConfig()) == Approx(40.0f * RadiansPerDegree).margin(0.05f));

    ScratchData StanceKey("wrist_stance");
    StanceKey.replace("items/weapons.json", R"("reach_m": 0.55,)", R"("reach_m": 0.55, "angle_deg": 40,)");
    StanceKey.replace("poses/stance_sword.json", R"("Pelvis": 0,)", R"("Pelvis": 0, "Weapon": -30,)");
    CHECK(getIdleWrist(StanceKey.makeConfig()) == Approx(-30.0f * RadiansPerDegree).margin(0.05f));
}

TEST_CASE("Wrist: the weapon follows the Weapon key of the move's clip", "[combat][wrist][data]") {
    ScratchData Data("wrist_clip");
    const std::filesystem::path Clip = "poses/sword_slash.json";
    Data.write(Clip, insertAfterEach(readText(DataDir / Clip), R"("pose": { )", R"("Weapon": 60, )"));
    BattleConfig Config = Data.makeConfig();
    Config.Left = makeSwordsman();
    Battle Fight(Config);
    run(Fight, {}, {}, TicksPerSecond / 2);
    const float Idle = getWrist(getLeft(Fight), BodyPart::ForearmL);
    std::optional<float> Swung;
    for (int Tick = 0; Tick < TicksPerSecond && !Swung; ++Tick) {
        Fight.update({.Heavy = true}, {}, Dt);
        const FighterView& Left = getLeft(Fight);
        // Well into the clip, past its blend-in.
        if (Left.MoveId == "sword_slash" && Left.Phase == AttackPhase::Active) {
            Swung = getWrist(Left, BodyPart::ForearmL);
        }
    }
    REQUIRE(Swung.has_value());
    CHECK(Idle == Approx(0.0f).margin(0.05f));
    CHECK(*Swung == Approx(60.0f * RadiansPerDegree).margin(0.2f));
}

TEST_CASE("Wrist: a sword hit is the forearm's, with the arm and the sword behind it", "[combat][wrist][data]") {
    const MoveMeasure Result =
        measureMove({.MoveId = "sword_cut", .WeaponId = "short_sword", .WithDummy = true, .DataDir = DataDir});
    REQUIRE(Result.Hit);
    REQUIRE(Result.HitPart == BodyPart::Head);
    REQUIRE(Result.ApproachSpeed > 0.0f);

    // J = v * mA*mB/(mA+mB): the attacker's strike mass from the dummy's head.
    const StandConfig Stand = loadStandConfig(DataDir / "stand.json");
    const stats::BalanceTable Balance = stats::loadBalanceTable(DataDir / "balance.json");
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const std::vector<std::string> Sword = {"short_sword"};
    const stats::PhysicalProfile Attacker =
        stats::computeProfile(Stand.Attacker, stats::buildLoadout(Sword, Catalog), Balance);
    const stats::PhysicalProfile Dummy = stats::computeProfile(Stand.Dummy, {}, Balance);
    const float Reduced = Result.Impulse / Result.ApproachSpeed;
    const float HeadMass = Dummy.Parts[static_cast<size_t>(BodyPart::Head)].MassKg;
    const float StrikeMass = 1.0f / (1.0f / Reduced - 1.0f / HeadMass);
    // The forearm's profile mass includes the sword's 1.2 kg.
    const float ArmMass = Attacker.Parts[static_cast<size_t>(BodyPart::ForearmL)].MassKg +
                          Attacker.Parts[static_cast<size_t>(BodyPart::UpperArmL)].MassKg;
    CHECK(StrikeMass == Approx(ArmMass).epsilon(0.02));
    // A cut, not a touch: faster than the hit threshold by far.
    CHECK(Result.ApproachSpeed > 3.0f);
}

TEST_CASE("Wrist: a sword fight with a moving wrist is deterministic", "[combat][wrist][dod]") {
    ScratchData Data("wrist_determinism");
    const std::filesystem::path Clip = "poses/sword_cut.json";
    Data.write(Clip, insertAfterEach(readText(DataDir / Clip), R"("pose": { )", R"("Weapon": 25, )"));
    BattleConfig Config = Data.makeConfig();
    Config.Left = makeSwordsman();
    Config.Right = makeSwordsman();
    Battle First(Config);
    Battle Second(Config);
    size_t Hits = 0;
    for (int Tick = 0; Tick < 6 * TicksPerSecond; ++Tick) {
        const float Distance = getRight(First).Position.X - getLeft(First).Position.X;
        const PlayerCommands LeftCmd{.MoveX = Distance > HeavyRange ? 1.0f : 0.0f, .Light = Tick % 41 == 0};
        const PlayerCommands RightCmd{.Light = Tick % 53 == 0};
        First.update(LeftCmd, RightCmd, Dt);
        Second.update(LeftCmd, RightCmd, Dt);
        Hits += getHits(First).size();
        for (size_t Index = 0; Index < 2; ++Index) {
            const FighterView& One = First.getSnapshot().Fighters[Index];
            const FighterView& Two = Second.getSnapshot().Fighters[Index];
            REQUIRE(One.Weapons.size() == Two.Weapons.size());
            for (size_t Weapon = 0; Weapon < One.Weapons.size(); ++Weapon) {
                REQUIRE(One.Weapons[Weapon].Position.X == Two.Weapons[Weapon].Position.X);
                REQUIRE(One.Weapons[Weapon].Angle == Two.Weapons[Weapon].Angle);
            }
            REQUIRE(One.Hp == Two.Hp);
        }
    }
    CHECK(Hits > 0);
}
