#include "ai/controller.hpp"

namespace fighter::ai {

combat::PlayerCommands HumanController::getCommands(const combat::RenderSnapshot&, double) {
    return Read ? Read() : combat::PlayerCommands{};
}

combat::PlayerCommands ReplayController::getCommands(const combat::RenderSnapshot&, double) {
    if (isFinished()) return {};
    return Record[Position++];
}

combat::PlayerCommands RecordingController::getCommands(const combat::RenderSnapshot& State, double Dt) {
    const combat::PlayerCommands Commands = Inner->getCommands(State, Dt);
    Record.push_back(Commands);
    return Commands;
}

} // namespace fighter::ai
