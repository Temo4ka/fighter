#include "ai/repertoire.hpp"

#include <algorithm>

namespace fighter::ai {
namespace {

bool hasInput(const std::vector<AiMove>& Attacks, const combat::MoveInput& Input);

} // namespace

std::string_view Repertoire::getHeight(std::string_view MoveId) const {
    const auto Found = Heights.find(MoveId);
    return Found == Heights.end() ? std::string_view{"mid"} : std::string_view{Found->second};
}

const combat::MoveSet& selectMoveSet(const combat::MoveLibrary& Library, const stats::Loadout& Gear) {
    return Library.selectSet(Gear.getMoveSet(stats::EquipmentSlot::MainHand),
                             Gear.getMoveSet(stats::EquipmentSlot::OffHand),
                             !Gear.findShield(stats::EquipmentSlot::OffHand));
}

Repertoire buildRepertoire(const combat::MoveLibrary& Library, const combat::MoveSet& Set, const AiConfig& Config) {
    Repertoire Result;
    for (const combat::MoveDef& Move : Library.getMoves()) {
        const std::string_view Height = Move.getHeight();
        if (!Height.empty()) Result.Heights.emplace(Move.Id, Height);
    }
    for (const combat::MoveSet* Current = &Set; Current; Current = Library.findSet(Current->Inherit)) {
        for (const combat::MoveSetEntry& Entry : Current->Entries) {
            if (hasInput(Result.Attacks, Entry.Input)) continue;
            const combat::MoveDef* Move = Library.findMove(Entry.MoveId);
            if (!Move) continue;
            AiMove Attack{.MoveId = Move->Id,
                          .Input = Entry.Input,
                          .MinRangeM = Config.DefaultMinRangeM,
                          .MaxRangeM = Config.DefaultMaxRangeM};
            if (Move->Intent) {
                if (Move->Intent->MaxRangeM > 0.0f) {
                    Attack.MinRangeM = Move->Intent->MinRangeM;
                    Attack.MaxRangeM = Move->Intent->MaxRangeM;
                }
                Attack.Role = Move->Intent->Role;
                Attack.Weight = Move->Intent->Weight;
            }
            Result.Attacks.push_back(std::move(Attack));
        }
    }
    return Result;
}

combat::PlayerCommands makeAttackCommands(const combat::MoveInput& Input, bool FacingRight) {
    using combat::InputDirection;
    combat::PlayerCommands Cmd;
    const float Forward = FacingRight ? 1.0f : -1.0f;
    switch (Input.Direction) {
        case InputDirection::Forward: Cmd.MoveX = Forward; break;
        case InputDirection::Back: Cmd.MoveX = -Forward; break;
        case InputDirection::Up: Cmd.Up = true; break;
        case InputDirection::Down: Cmd.Down = true; break;
        case InputDirection::UpForward: Cmd.Up = true; Cmd.MoveX = Forward; break;
        case InputDirection::UpBack: Cmd.Up = true; Cmd.MoveX = -Forward; break;
        case InputDirection::DownForward: Cmd.Down = true; Cmd.MoveX = Forward; break;
        case InputDirection::DownBack: Cmd.Down = true; Cmd.MoveX = -Forward; break;
        case InputDirection::Neutral:
        case InputDirection::Count: break;
    }
    Cmd.Light = Input.Buttons.contains(combat::AttackButton::Light);
    Cmd.Heavy = Input.Buttons.contains(combat::AttackButton::Heavy);
    Cmd.Kick = Input.Buttons.contains(combat::AttackButton::Kick);
    Cmd.Special = Input.Buttons.contains(combat::AttackButton::Special);
    return Cmd;
}

combat::PlayerCommands makeBlockCommands(std::string_view Height) {
    combat::PlayerCommands Cmd;
    Cmd.Block = true;
    if (Height == "high") Cmd.Up = true;
    if (Height == "low") Cmd.Down = true;
    return Cmd;
}

namespace {

bool hasInput(const std::vector<AiMove>& Attacks, const combat::MoveInput& Input) {
    return std::ranges::any_of(Attacks, [&](const AiMove& Each) { return Each.Input == Input; });
}

} // namespace

} // namespace fighter::ai
