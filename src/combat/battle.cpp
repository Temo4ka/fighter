#include "combat/battle.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "debug/draw.hpp"

namespace fighter::combat {
namespace {

// Parameters of the kinematic placeholder. Body physics replaces them in phase 2.
constexpr float WalkSpeed = 3.0f;    // m/s
constexpr float JumpSpeed = 5.0f;    // m/s
constexpr float HalfWidth = 0.25f;   // m
constexpr float Height = 1.8f;       // m
constexpr float StartX = 2.0f;       // m from the arena center

[[maybe_unused]] const char* getPlayerName(std::size_t I) { return I == 0 ? "P1" : "P2"; }

} // namespace

Battle::Battle(const BattleConfig& Config) : Cfg(Config) {
    const stats::BalanceTable Balance = stats::BalanceTable::getDefaults();
    const std::array<const FighterConfig*, 2> Configs = {&Cfg.Left, &Cfg.Right};

    for (std::size_t I = 0; I < Fighters.size(); ++I) {
        FighterState& F = Fighters[I];
        F.Profile = stats::computeProfile(Configs[I]->Stats, Configs[I]->Loadout, Balance);
        F.Hp = F.Profile.MaxHp;
        F.Position = {I == 0 ? -StartX : StartX, 0.0f};
        F.FacingRight = (I == 0);
    }
    publishSnapshot();
}

void Battle::update(const PlayerCommands& LeftCmd, const PlayerCommands& RightCmd, double Dt) {
    if (Result) return;

    const float StepDt = static_cast<float>(Dt);
    updateFighter(Fighters[0], LeftCmd, StepDt);
    updateFighter(Fighters[1], RightCmd, StepDt);

    // Fighters face each other.
    Fighters[0].FacingRight = Fighters[0].Position.X <= Fighters[1].Position.X;
    Fighters[1].FacingRight = !Fighters[0].FacingRight;

    ElapsedSec += Dt;
    ++Tick;

    if (ElapsedSec >= Cfg.RoundTimeSec) {
        const float HpL = Fighters[0].Hp;
        const float HpR = Fighters[1].Hp;
        finish(HpL > HpR ? Winner::Left : HpR > HpL ? Winner::Right : Winner::Draw);
    }

    publishSnapshot();
    drawDebug();
}

void Battle::updateFighter(FighterState& F, const PlayerCommands& Cmd, float Dt) {
    F.Velocity.X = std::clamp(Cmd.MoveX, -1.0f, 1.0f) * WalkSpeed;
    if (Cmd.Jump && F.Grounded) {
        F.Velocity.Y = JumpSpeed;
        F.Grounded = false;
    }
    if (!F.Grounded) F.Velocity += Cfg.Arena.Gravity * Dt;

    F.Position += F.Velocity * Dt;

    if (F.Position.Y <= 0.0f) {
        F.Position.Y = 0.0f;
        F.Velocity.Y = 0.0f;
        F.Grounded = true;
    }
    const float Limit = Cfg.Arena.HalfWidthM - HalfWidth;
    F.Position.X = std::clamp(F.Position.X, -Limit, Limit);
}

void Battle::finish(Winner W) {
    BattleResult R;
    R.WinnerSide = W;
    R.TimeSec = ElapsedSec;
    R.Left.HpLeft = Fighters[0].Hp;
    R.Right.HpLeft = Fighters[1].Hp;
    Result = R;
}

void Battle::publishSnapshot() {
    Snapshot.Tick = Tick;
    Snapshot.TimeLeftSec = std::max(0.0, Cfg.RoundTimeSec - ElapsedSec);
    Snapshot.Arena.HalfWidthM = Cfg.Arena.HalfWidthM;
    for (std::size_t I = 0; I < Fighters.size(); ++I) {
        const FighterState& F = Fighters[I];
        FighterView& V = Snapshot.Fighters[I];
        V.Position = F.Position;
        V.Size = {2.0f * HalfWidth, Height};
        V.FacingRight = F.FacingRight;
        V.Hp = F.Hp;
        V.MaxHp = F.Profile.MaxHp;
    }
}

void Battle::drawDebug() const {
    if constexpr (FIGHTER_DEBUG) {
        const float Hw = Cfg.Arena.HalfWidthM;
        debug::drawLine(debug::Cat::Static, {-Hw, 0.0f}, {Hw, 0.0f});
        debug::drawLine(debug::Cat::Static, {-Hw, 0.0f}, {-Hw, 4.0f});
        debug::drawLine(debug::Cat::Static, {Hw, 0.0f}, {Hw, 4.0f});

        for (std::size_t I = 0; I < Fighters.size(); ++I) {
            const FighterState& F = Fighters[I];
            debug::ScopedSide Owner(I == 0 ? debug::Side::Left : debug::Side::Right);

            const Vec2 P = F.Position;
            const std::array<Vec2, 4> Box = {
                Vec2{P.X - HalfWidth, P.Y},
                Vec2{P.X + HalfWidth, P.Y},
                Vec2{P.X + HalfWidth, P.Y + Height},
                Vec2{P.X - HalfWidth, P.Y + Height},
            };
            debug::drawPoly(debug::Cat::Hurtbox, Box);

            const Vec2 CenterOfMass = P + Vec2{0.0f, Height * 0.55f};
            debug::drawCross(debug::Cat::CoM, CenterOfMass);
            if (F.Velocity.getLengthSquared() > 1e-4f) {
                debug::drawArrow(debug::Cat::Velocity, CenterOfMass, F.Velocity * 0.2f,
                                 std::format("{:.1f} m/s", F.Velocity.getLength()));
            }

            debug::setPanel(std::format("{} pos", getPlayerName(I)),
                            std::format("({:+.2f}, {:+.2f}) m  {}", P.X, P.Y,
                                        F.Grounded ? "ground" : "air"));
            debug::setPanel(std::format("{} hp", getPlayerName(I)),
                            std::format("{:.0f} / {:.0f}", F.Hp, F.Profile.MaxHp));
        }
        debug::setPanel("round", std::format("{:.1f} s left", Snapshot.TimeLeftSec));
    }
}

} // namespace fighter::combat
