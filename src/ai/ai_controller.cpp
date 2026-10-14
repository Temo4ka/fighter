#include "ai/ai_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>

#include "debug/draw.hpp"

namespace fighter::ai {
namespace {

using combat::AttackPhase;
using combat::FighterState;
using combat::FighterView;
using combat::PlayerCommands;

/// One step of splitmix64: a small generator whose whole state is one number.
uint64_t nextRandom(uint64_t& RngState);

/// Can the fighter act: not hit, not on the floor, not out.
bool canAct(const FighterView& View);

/// Is the fighter in the part of a strike that a block has to meet?
bool isStriking(const FighterView& View);

} // namespace

std::string_view getAiStateName(AiState State) {
    constexpr std::array<std::string_view, 5> Names = {"Approach", "Hold", "Attack", "Block", "Recover"};
    const auto Index = static_cast<size_t>(State);
    return Index < Names.size() ? Names[Index] : "?";
}

AiController::AiController(size_t NewSide, AiConfig NewConfig, Repertoire NewRepertoire)
    : Side(NewSide), Config(NewConfig), Moves(std::move(NewRepertoire)), RngState(NewConfig.Seed) {}

float AiController::rollChance() {
    constexpr float Scale = 1.0f / 16777216.0f;   // 2^-24
    return static_cast<float>(nextRandom(RngState) >> 40) * Scale;
}

PlayerCommands AiController::getCommands(const combat::RenderSnapshot& State, double Dt) {
    const FighterView& Me = State.Fighters[Side];
    const FighterView& Opponent = State.Fighters[1 - Side];
    const float DistanceM = std::abs(Opponent.Position.X - Me.Position.X);
    const float Toward = Opponent.Position.X >= Me.Position.X ? 1.0f : -1.0f;
    StateSec += Dt;

    const bool BlockNow = shouldBlock(Opponent, DistanceM, Dt);
    const bool OpponentOut = Opponent.State == FighterState::KnockedOut;

    // Transitions.
    if (!canAct(Me) || OpponentOut) {
        if (Current != AiState::Recover) enter(AiState::Recover);
    } else if (BlockNow && Current != AiState::Attack && Me.State != FighterState::Attacking) {
        if (Current != AiState::Block) enter(AiState::Block);
    } else {
        // A second pass lets a state that was just entered act in the same step.
        advance(Me, DistanceM);
        advance(Me, DistanceM);
    }

    // Output.
    PlayerCommands Cmd;
    switch (Current) {
        case AiState::Approach: Cmd.MoveX = Toward; break;
        case AiState::Hold:
            if (!hasAttackInRange(DistanceM, 0.0f) && DistanceM < getNearestReachM() && !Me.AgainstWall) {
                Cmd.MoveX = -Toward;   // too close for every strike: back off
            }
            break;
        case AiState::Attack:
            if (Chosen) Cmd = makeAttackCommands(Moves.Attacks[*Chosen].Input, Me.FacingRight);
            break;
        case AiState::Block: Cmd = makeBlockCommands(BlockHeight); break;
        case AiState::Recover: break;
    }
    publish(State, DistanceM);
    return Cmd;
}

void AiController::advance(const FighterView& Me, float DistanceM) {
    switch (Current) {
        case AiState::Approach:
            if (hasAttackInRange(DistanceM, Config.RangeMarginM) || DistanceM < getNearestReachM()) {
                enter(AiState::Hold);
                StateSec = Config.DecisionIntervalSec;   // decide at once
            }
            break;
        case AiState::Hold:
            if (!hasAttackInRange(DistanceM, 0.0f)) {
                if (DistanceM >= getNearestReachM()) {
                    enter(AiState::Approach);
                } else if (Me.AgainstWall) {
                    // Cannot back off: strike with the shortest move.
                    Chosen = findClosestAttack();
                    if (Chosen) enter(AiState::Attack);
                }
            } else if (StateSec >= Config.DecisionIntervalSec) {
                StateSec = 0.0;
                const bool HasStamina = Me.MaxStamina <= 0.0f || Me.Stamina >= Me.MaxStamina * Config.MinStaminaFraction;
                if (HasStamina && rollChance() < Config.AttackChance) {
                    Chosen = pickAttack(DistanceM);
                    if (Chosen) enter(AiState::Attack);
                }
            }
            break;
        case AiState::Attack:
            if (StateSec >= Config.PressSec) enter(AiState::Recover);
            break;
        case AiState::Block:
            enter(AiState::Recover);   // the strike is over and the hold has passed
            break;
        case AiState::Recover:
            if (StateSec >= Config.RecoverSec && Me.State != FighterState::Attacking) {
                enter(hasAttackInRange(DistanceM, 0.0f) ? AiState::Hold : AiState::Approach);
            }
            break;
    }
}

void AiController::enter(AiState Next) {
    if (Next == Current) return;
    debug::logEvent(std::format("P{} ai: {} -> {}", Side + 1, getAiStateName(Current), getAiStateName(Next)));
    Current = Next;
    StateSec = 0.0;
}

bool AiController::shouldBlock(const FighterView& Opponent, float DistanceM, double Dt) {
    if (isStriking(Opponent)) {
        SinceStrikeSec = 0.0;
        if (!Watching || Opponent.MoveId != WatchedMove) {
            Watching = true;
            WatchedMove = Opponent.MoveId;
            WatchedSec = 0.0;
            BlockPlanned = rollChance() < Config.BlockChance;
        } else {
            WatchedSec += Dt;
        }
        BlockHeight = std::string(Moves.getHeight(WatchedMove));
    } else {
        Watching = false;
        SinceStrikeSec += Dt;
        if (SinceStrikeSec >= Config.BlockHoldSec) BlockPlanned = false;
    }
    if (!BlockPlanned || DistanceM > Config.BlockRangeM) return false;
    // Noticed the strike a reaction time after it started; once raised, the
    // block stays until the hold time after the strike has passed.
    return Watching ? WatchedSec >= Config.ReactionDelaySec : true;
}

std::optional<size_t> AiController::pickAttack(float DistanceM) {
    float Total = 0.0f;
    for (const AiMove& Attack : Moves.Attacks) {
        if (Attack.isInRange(DistanceM)) Total += std::max(Attack.Weight, 0.0f);
    }
    if (Total <= 0.0f) return std::nullopt;
    float Roll = rollChance() * Total;
    std::optional<size_t> Last;
    for (auto&& [Index, Attack] : std::views::zip(std::views::iota(size_t{0}), Moves.Attacks)) {
        if (!Attack.isInRange(DistanceM) || Attack.Weight <= 0.0f) continue;
        Last = Index;
        Roll -= Attack.Weight;
        if (Roll < 0.0f) break;
    }
    return Last;
}

bool AiController::hasAttackInRange(float DistanceM, float MarginM) const {
    return std::ranges::any_of(Moves.Attacks, [&](const AiMove& Attack) {
        return DistanceM >= Attack.MinRangeM && DistanceM <= Attack.MaxRangeM - MarginM;
    });
}

std::optional<size_t> AiController::findClosestAttack() const {
    std::optional<size_t> Best;
    for (auto&& [Index, Attack] : std::views::zip(std::views::iota(size_t{0}), Moves.Attacks)) {
        if (!Best || Attack.MinRangeM < Moves.Attacks[*Best].MinRangeM) Best = Index;
    }
    return Best;
}

float AiController::getFarthestReachM() const {
    float Farthest = Moves.Attacks.empty() ? Config.DefaultMaxRangeM : 0.0f;
    for (const AiMove& Attack : Moves.Attacks) Farthest = std::max(Farthest, Attack.MaxRangeM);
    return Farthest;
}

float AiController::getNearestReachM() const {
    float Nearest = Moves.Attacks.empty() ? Config.DefaultMinRangeM : Moves.Attacks.front().MinRangeM;
    for (const AiMove& Attack : Moves.Attacks) Nearest = std::min(Nearest, Attack.MinRangeM);
    return Nearest;
}

void AiController::publish(const combat::RenderSnapshot& State, float DistanceM) const {
    const FighterView& Me = State.Fighters[Side];
    const float Toward = State.Fighters[1 - Side].Position.X >= Me.Position.X ? 1.0f : -1.0f;
    const AiMove* Move = getChosenMove();
    debug::setPanel(std::format("P{} ai", Side + 1),
                    std::format("{} {:.1f}s  dist {:.2f} m  move {}  block {}{}", getAiStateName(Current), StateSec, DistanceM,
                                Move ? Move->MoveId : "-", BlockPlanned ? "planned " : "no ", BlockPlanned ? BlockHeight : ""));
    // The reach of the repertoire on the floor, and the state over the head.
    const float Reach = getFarthestReachM();
    debug::drawLine(debug::Cat::Trajectory, {Me.Position.X, 0.03f}, {Me.Position.X + Toward * Reach, 0.03f});
    debug::drawCross(debug::Cat::Trajectory, {Me.Position.X + Toward * Reach, 0.03f}, 0.05f);
    debug::drawText(debug::Cat::Trajectory, {Me.Position.X, Me.Position.Y + Me.Size.Y + 0.15f},
                    std::format("ai {}", getAiStateName(Current)));
}

namespace {

uint64_t nextRandom(uint64_t& RngState) {
    RngState += 0x9E3779B97F4A7C15ull;
    uint64_t Z = RngState;
    Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
    Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
    return Z ^ (Z >> 31);
}

bool canAct(const FighterView& View) {
    switch (View.State) {
        case FighterState::Reacting:
        case FighterState::KnockedDown:
        case FighterState::GettingUp:
        case FighterState::KnockedOut: return false;
        default: return true;
    }
}

bool isStriking(const FighterView& View) {
    return View.State == FighterState::Attacking &&
           (View.Phase == AttackPhase::Startup || View.Phase == AttackPhase::Active);
}

} // namespace

} // namespace fighter::ai
