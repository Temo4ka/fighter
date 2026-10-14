#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ai/controller.hpp"

using namespace fighter::ai;
using fighter::combat::PlayerCommands;
using fighter::combat::RenderSnapshot;

namespace {

/// Gives the commands of a list, repeating the last one.
class ListController : public Controller {
public:
    explicit ListController(std::vector<PlayerCommands> NewList) : List(std::move(NewList)) {}

    PlayerCommands getCommands(const RenderSnapshot&, double) override {
        const size_t Index = std::min(Next++, List.size() - 1);
        return List[Index];
    }
    std::string_view getName() const override { return "list"; }

private:
    std::vector<PlayerCommands> List;
    size_t Next = 0;
};

PlayerCommands makeMove(float MoveX, bool Light) {
    PlayerCommands Cmd;
    Cmd.MoveX = MoveX;
    Cmd.Light = Light;
    return Cmd;
}

} // namespace

TEST_CASE("HumanController: passes on what the device gives", "[ai][controller]") {
    PlayerCommands Held = makeMove(1.0f, false);
    HumanController Human([&Held] { return Held; });
    const RenderSnapshot State;
    CHECK(Human.getCommands(State, 1.0 / 60.0) == Held);
    Held = makeMove(-1.0f, true);
    CHECK(Human.getCommands(State, 1.0 / 60.0) == Held);
    CHECK(Human.getName() == "human");
}

TEST_CASE("HumanController: no device gives no input", "[ai][controller]") {
    HumanController Human(nullptr);
    CHECK(Human.getCommands(RenderSnapshot{}, 1.0 / 60.0) == PlayerCommands{});
}

TEST_CASE("ReplayController: plays the record, then nothing, and rewinds", "[ai][controller]") {
    const std::vector<PlayerCommands> Record = {makeMove(1.0f, false), makeMove(0.0f, true)};
    ReplayController Replay(Record);
    const RenderSnapshot State;
    CHECK(Replay.getName() == "replay");
    CHECK_FALSE(Replay.isFinished());
    CHECK(Replay.getCommands(State, 0.0) == Record[0]);
    CHECK(Replay.getCommands(State, 0.0) == Record[1]);
    CHECK(Replay.getPosition() == 2);
    CHECK(Replay.isFinished());
    CHECK(Replay.getCommands(State, 0.0) == PlayerCommands{});
    Replay.rewind();
    CHECK(Replay.getPosition() == 0);
    CHECK(Replay.getCommands(State, 0.0) == Record[0]);
}

TEST_CASE("RecordingController: writes down what the inner one gives", "[ai][controller]") {
    const std::vector<PlayerCommands> Script = {makeMove(1.0f, false), makeMove(0.5f, true), makeMove(0.0f, false)};
    ListController Inner(Script);
    RecordingController Recorder(Inner);
    const RenderSnapshot State;
    CHECK(Recorder.getName() == "list");
    for (const PlayerCommands& Expected : Script) CHECK(Recorder.getCommands(State, 0.0) == Expected);
    CHECK(Recorder.getRecord() == Script);
}
