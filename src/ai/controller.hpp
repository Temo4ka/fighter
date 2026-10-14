//===- ai/controller.hpp - Command sources ----------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares Controller, a source of the commands of one fighter, and
/// two simple ones: HumanController and ReplayController (task 2.9, formerly
/// 4.1). The AI is AiController (ai/ai_controller.hpp).
///
/// A controller is asked once per simulation step, with the snapshot after the
/// previous step, and returns the PlayerCommands that Battle::update() takes.
/// It never sees physics, only the snapshot, so a human, an AI and a replay
/// are interchangeable.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "combat/commands.hpp"
#include "combat/snapshot.hpp"

namespace fighter::ai {

class Controller {
public:
    virtual ~Controller() = default;

    /// The commands for the next step of \p Dt seconds, given the state after
    /// the previous one.
    virtual combat::PlayerCommands getCommands(const combat::RenderSnapshot& State, double Dt) = 0;

    /// A short name for the debug panel: "human", "replay", "ai".
    virtual std::string_view getName() const = 0;
};

/// Passes on the commands a device gives (the keyboard of the sandbox).
class HumanController : public Controller {
public:
    using Source = std::function<combat::PlayerCommands()>;

    explicit HumanController(Source NewSource) : Read(std::move(NewSource)) {}

    combat::PlayerCommands getCommands(const combat::RenderSnapshot& State, double Dt) override;
    std::string_view getName() const override { return "human"; }

private:
    Source Read;
};

/// Plays back a recorded list of commands, one per step; after the end it
/// gives no input.
class ReplayController : public Controller {
public:
    explicit ReplayController(std::vector<combat::PlayerCommands> NewRecord) : Record(std::move(NewRecord)) {}

    combat::PlayerCommands getCommands(const combat::RenderSnapshot& State, double Dt) override;
    std::string_view getName() const override { return "replay"; }

    /// The steps played so far.
    size_t getPosition() const { return Position; }
    bool isFinished() const { return Position >= Record.size(); }
    /// Starts from the first step again.
    void rewind() { Position = 0; }

private:
    std::vector<combat::PlayerCommands> Record;
    size_t Position = 0;
};

/// Passes on the commands of another controller and writes them down, so
/// that a fight can be replayed with ReplayController.
class RecordingController : public Controller {
public:
    explicit RecordingController(Controller& NewInner) : Inner(&NewInner) {}

    combat::PlayerCommands getCommands(const combat::RenderSnapshot& State, double Dt) override;
    std::string_view getName() const override { return Inner->getName(); }

    const std::vector<combat::PlayerCommands>& getRecord() const { return Record; }

private:
    Controller* Inner;
    std::vector<combat::PlayerCommands> Record;
};

} // namespace fighter::ai
