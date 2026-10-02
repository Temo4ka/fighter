#include "app/demo_script.hpp"

namespace fighter::app {
namespace {

constexpr uint64_t TicksPerSecond = 60;
/// Attack buttons are held this long, like a quick key press.
constexpr uint64_t PressTicks = 3;
/// P1 attacks once in this many ticks while in range.
constexpr uint64_t AttackPeriod = TicksPerSecond * 2 / 3;
/// Distance between the fighters (floor points) at which P1 stops walking
/// and attacks, m.
constexpr float JabRange = 0.72f;
constexpr float KickRange = 0.95f;

/// Is a button pressed on \p Tick if it is pressed once every \p Period ticks?
bool isPressedEvery(uint64_t Tick, uint64_t Period) { return Tick % Period < PressTicks; }

float getDistance(const combat::RenderSnapshot& State) {
    return State.Fighters[1].Position.X - State.Fighters[0].Position.X;
}

} // namespace

std::optional<DemoScript> findDemoScript(std::string_view Name) {
    if (Name == "walk") return DemoScript::Walk;
    if (Name == "fight") return DemoScript::Fight;
    if (Name == "kick") return DemoScript::Kick;
    return std::nullopt;
}

DemoInput getDemoInput(DemoScript Script, uint64_t Tick, const combat::RenderSnapshot& State) {
    DemoInput Input;
    combat::PlayerCommands& Left = Input.Left;

    switch (Script) {
        case DemoScript::Walk: {
            // 0.5 s stand, 2.5 s forward, 0.5 s stand, 2.5 s back.
            const uint64_t Phase = Tick % (TicksPerSecond * 6);
            if (Phase >= TicksPerSecond / 2 && Phase < TicksPerSecond * 3) Left.MoveX = 1.0f;
            if (Phase >= TicksPerSecond * 7 / 2) Left.MoveX = -1.0f;
            // The right fighter moves the same way (backwards, then forwards)
            // to keep the distance.
            Input.Right.MoveX = Left.MoveX;
            break;
        }
        case DemoScript::Fight: {
            // Walk into range, then a combo: three jabs and a kick.
            if (getDistance(State) > JabRange) {
                Left.MoveX = 1.0f;
                break;
            }
            const bool KickTurn = Tick / AttackPeriod % 4 == 3;
            const bool Press = isPressedEvery(Tick, AttackPeriod);
            Left.Punch = Press && !KickTurn;
            Left.Kick = Press && KickTurn;
            break;
        }
        case DemoScript::Kick:
            if (getDistance(State) > KickRange) {
                Left.MoveX = 1.0f;
                break;
            }
            Left.Kick = isPressedEvery(Tick, TicksPerSecond * 3 / 2);
            break;
    }
    return Input;
}

} // namespace fighter::app
