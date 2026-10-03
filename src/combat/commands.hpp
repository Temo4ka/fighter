//===- combat/commands.hpp - Player input for one step ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines PlayerCommands, the input of one player for one
/// simulation step (docs/DEVELOPMENT_PLAN.md, section 4 and task 2.0.2), and
/// the rules that read a block zone and crouching out of it (decisions O.1-O.3).
///
/// It is button state ("is it held right now"), not events. A keyboard, a
/// gamepad, an AI or a replay fill it the same way. Directions are in world
/// space (MoveX > 0 is to the right); what is "forward" depends on where the
/// fighter faces, so the battle interprets them.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

namespace fighter::combat {

struct PlayerCommands {
    float MoveX = 0.0f;   ///< -1 ... 1, world space: > 0 is to the right.
    bool Up = false;      ///< Selects the high block zone; a jump later (O.3).
    bool Down = false;    ///< Crouch, or the low block zone while blocking.
    bool Block = false;
    bool Jab = false;
    bool HeavyPunch = false;
    bool BodyKick = false;
    bool LowKick = false;

    constexpr bool operator==(const PlayerCommands&) const = default;
};

/// The part of the body a block covers (decision O.2).
enum class BlockZone : uint8_t {
    High,   ///< The head.
    Mid,    ///< The torso and the arms.
    Low,    ///< The pelvis and the legs.
};

/// Stick input below this does not count as a direction.
inline constexpr float MoveDeadZone = 0.1f;

/// The block zone the commands ask for, or nullopt if the block button is
/// not held. While blocking, the vertical keys choose the zone:
///
///   block + down (with or without a horizontal direction) -> Low;
///   block + up + forward                                  -> Mid;
///   block + up (without forward)                          -> High;
///   block alone or with a horizontal direction            -> Mid.
///
/// "Forward" is towards the opponent: for a fighter facing left it is
/// MoveX < 0. Down wins over up when both are held.
constexpr std::optional<BlockZone> getBlockZone(const PlayerCommands& Cmd, bool FacingRight) {
    if (!Cmd.Block) return std::nullopt;
    if (Cmd.Down) return BlockZone::Low;
    const float Forward = FacingRight ? Cmd.MoveX : -Cmd.MoveX;
    if (Cmd.Up && Forward <= MoveDeadZone) return BlockZone::High;
    return BlockZone::Mid;
}

/// Do the commands ask to crouch? While blocking, down selects the low zone
/// instead (getBlockZone()).
constexpr bool isCrouching(const PlayerCommands& Cmd) { return Cmd.Down && !Cmd.Block; }

} // namespace fighter::combat
