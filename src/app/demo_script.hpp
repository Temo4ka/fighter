//===- app/demo_script.hpp - Scripted input ---------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the demo scripts of the sandbox (--demo <name>): input
/// generated from the simulation tick and the last snapshot instead of the
/// keyboard.
///
/// They let you watch the fighters without playing and, together with
/// --screenshot, give reproducible frames: the simulation is deterministic,
/// so the same script and frame number always show the same state.
///
/// Scripts:
///  - walk:  P1 walks forward, then back, in a loop; P2 moves the same way;
///  - fight: P1 walks into range of P2 (a standing dummy) and attacks:
///           three jabs from close, then a kick from kicking range (it
///           steps back for it); a clean kick knocks the dummy down;
///  - kick:  P1 walks into kicking range of P2 and kicks.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "combat/commands.hpp"
#include "combat/snapshot.hpp"

namespace fighter::app {

enum class DemoScript : uint8_t { Walk, Fight, Kick };

struct DemoInput {
    combat::PlayerCommands Left;
    combat::PlayerCommands Right;
};

std::optional<DemoScript> findDemoScript(std::string_view Name);

/// Input of both players on simulation step \p Tick, given the state after
/// the previous step.
DemoInput getDemoInput(DemoScript Script, uint64_t Tick, const combat::RenderSnapshot& State);

} // namespace fighter::app
