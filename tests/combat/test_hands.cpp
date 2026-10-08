#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

namespace {

/// A fighter of the shipped stats with \p Items (ids, or {"id", "slot"}).
FighterConfig makeFighter(const std::vector<stats::ItemRef>& Items) {
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(std::filesystem::path(FIGHTER_DATA_DIR) / "items");
    FighterConfig Config;
    Config.Loadout = stats::buildLoadout(Items, Catalog);
    return Config;
}

/// P1 walks up to \p Range and presses \p Cmd whenever it is free, for
/// \p Ticks; P2 holds \p VictimCmd. Returns P1's strikes that landed.
std::vector<StrikeLanded> attack(Battle& Fight, const PlayerCommands& Cmd, float Range, const PlayerCommands& VictimCmd,
                                 int Ticks) {
    std::vector<StrikeLanded> Landed;
    for (int Tick = 0; Tick < Ticks && !Fight.getResult(); ++Tick) {
        const FighterView& Left = getLeft(Fight);
        PlayerCommands Next;
        if (getRight(Fight).Position.X - Left.Position.X > Range) {
            Next.MoveX = 1.0f;
        } else if (Left.State == FighterState::Idle || Left.State == FighterState::Walking) {
            Next = Cmd;
        }
        Fight.update(Next, VictimCmd, Dt);
        for (const BattleEvent& Event : Fight.getEvents()) {
            const auto* Hit = std::get_if<StrikeLanded>(&Event);
            if (Hit && Hit->Contact.Attacker.Fighter == 0) Landed.push_back(*Hit);
        }
    }
    return Landed;
}

} // namespace

TEST_CASE("Hands: a sword in the left hand strikes with the left arm", "[combat][hands][data]") {
    BattleConfig Config = makeConfig();
    Config.Left = makeFighter({{.Id = "short_sword", .Slot = stats::EquipmentSlot::OffHand}});
    Battle Fight(Config);
    const std::vector<PlayerCommands> Heavy = {{.Heavy = true}};
    CHECK(findFirstStart(Fight, Heavy, 1).MoveId == "sword_slash");

    Battle Duel(Config);
    const std::vector<StrikeLanded> Hits = attack(Duel, {.Heavy = true}, HeavyRange, {}, 4 * TicksPerSecond);
    REQUIRE_FALSE(Hits.empty());
    for (const StrikeLanded& Hit : Hits) {
        CHECK(Hit.MoveId == "sword_slash");
        const BodyPart Part = Hit.Contact.Attacker.Part;
        CHECK((Part == BodyPart::ForearmL || Part == BodyPart::UpperArmL));
    }
}

TEST_CASE("Hands: both hands hold a two-handed weapon", "[combat][hands][data]") {
    const auto getForearmsApart = [](const FighterConfig& Who) {
        BattleConfig Config = makeConfig();
        Config.Left = Who;
        Battle Fight(Config);
        run(Fight, {}, {}, TicksPerSecond);
        const FighterView& View = getLeft(Fight);
        return (getPart(View, BodyPart::ForearmL).Position - getPart(View, BodyPart::ForearmR).Position).getLength();
    };
    const float Unarmed = getForearmsApart(FighterConfig{});
    const float Gripped = getForearmsApart(makeFighter({{.Id = "greatsword"}}));
    // The left fist is on the handle, a hand's width past the right one.
    CHECK(Gripped < 0.2f);
    CHECK(Gripped < Unarmed);
}

TEST_CASE("Hands: the shield and the pair's block stop a jab in the middle guard", "[combat][hands][data]") {
    // The sword and shield guard (the shield in the off hand, the middle
    // guard covers the head too) stops the jab and lets little damage
    // through.
    const auto jabAt = [](const FighterConfig& Victim, const PlayerCommands& Guard) {
        BattleConfig Config = makeConfig();
        Config.Right = Victim;
        Battle Fight(Config);
        run(Fight, {}, Guard, 2);
        return attack(Fight, {.Light = true}, JabRange, Guard, 3 * TicksPerSecond);
    };
    const FighterConfig Shielded = makeFighter({{.Id = "short_sword"}, {.Id = "wooden_shield"}});
    const std::vector<StrikeLanded> Clean = jabAt(Shielded, {});
    const std::vector<StrikeLanded> Guarded = jabAt(Shielded, {.Block = true});
    REQUIRE_FALSE(Clean.empty());
    REQUIRE_FALSE(Guarded.empty());
    float CleanDamage = 0.0f;
    for (const StrikeLanded& Hit : Clean) CleanDamage = std::max(CleanDamage, Hit.Damage);
    for (const StrikeLanded& Hit : Guarded) {
        CHECK(Hit.Blocked);
        CHECK(Hit.Damage < CleanDamage * 0.5f);
    }
}

TEST_CASE("Hands: a shield strikes only from the main hand", "[combat][hands][data]") {
    // Decision 2026-10-08: in the off hand a shield guards and gives no
    // strikes, in the main hand it is a weapon with its own moveset.
    const auto firstSpecial = [](const FighterConfig& Who) {
        BattleConfig Config = makeConfig();
        Config.Left = Who;
        Battle Fight(Config);
        const std::vector<PlayerCommands> Special = {{.Special = true}};
        return findFirstStart(Fight, Special, TicksPerSecond / 2).MoveId;
    };
    CHECK(firstSpecial(makeFighter({{.Id = "wooden_shield"}})).empty());
    CHECK(firstSpecial(makeFighter({{.Id = "wooden_shield", .Slot = stats::EquipmentSlot::MainHand}})) ==
          "shield_bash");
    CHECK(firstSpecial(makeFighter({{.Id = "short_sword"}, {.Id = "wooden_shield"}})) == "sword_spin");
}

TEST_CASE("Hands: the small shield is torso-sized in the middle guard", "[combat][hands][data]") {
    // Decision 2026-10-08: wooden_shield is the small shield, it covers
    // about the torso's height and reaches no lower than the pelvis. The
    // plate lies along the forearm (angle_deg 0), so its height is the
    // length and width projected on the vertical.
    constexpr float TorsoHeightM = 0.49f;      // humanoid.json: 0.23 m capsule + 2 x 0.13 m radius
    constexpr float PelvisHalfHeightM = 0.11f; // 0.08 m half extent + 0.03 m radius
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(std::filesystem::path(FIGHTER_DATA_DIR) / "items");
    const stats::ShieldProps& Shield = *Catalog.findItem("wooden_shield")->Shield;

    BattleConfig Config = makeConfig();
    Config.Left = makeFighter({{.Id = "short_sword"}, {.Id = "wooden_shield"}});
    Battle Fight(Config);
    run(Fight, {.Block = true}, {}, TicksPerSecond);
    const FighterView& View = getLeft(Fight);
    const PartTransform& Forearm = getPart(View, BodyPart::ForearmL);
    const float HalfHeight =
        0.5f * (Shield.LengthM * std::abs(std::cos(Forearm.Angle)) + Shield.WidthM * std::abs(std::sin(Forearm.Angle)));
    CHECK(HalfHeight * 2.0f > TorsoHeightM * 0.8f);
    CHECK(HalfHeight * 2.0f < TorsoHeightM * 1.2f);
    CHECK(Forearm.Position.Y - HalfHeight > getPart(View, BodyPart::Pelvis).Position.Y + PelvisHalfHeightM);
}
