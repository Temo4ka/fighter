//===- combat/commands.hpp - Player input for one step ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines PlayerCommands, the input of one player for one
/// simulation step (docs/DEVELOPMENT_PLAN.md, section 4).
///
/// It is button state ("is it held right now"), not events. A keyboard, a
/// gamepad, an AI or a replay fill it the same way.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace fighter::combat {

struct PlayerCommands {
    float MoveX = 0.0f;   ///< -1 ... 1.
    bool Jump = false;
    bool Crouch = false;
    bool Block = false;
    bool Punch = false;
    bool Kick = false;

    constexpr bool operator==(const PlayerCommands&) const = default;
};

} // namespace fighter::combat
