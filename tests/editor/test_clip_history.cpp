#include <catch2/catch_test_macros.hpp>

#include "anim/clip.hpp"
#include "editor/clip_edit.hpp"
#include "editor/clip_history.hpp"

using namespace fighter;
using namespace fighter::editor;

namespace {

anim::Clip makeClip() {
    return anim::parseClip(R"({
      "duration": 0.5,
      "keys": [
        { "t": 0.0, "pose": { "Torso": 0 } },
        { "t": 0.3, "pose": { "Torso": 10 } }
      ] })",
                           "history");
}

} // namespace

TEST_CASE("Undo and redo walk the committed states", "[editor][history]") {
    ClipHistory History;
    anim::Clip Edited = makeClip();
    History.reset(Edited);
    CHECK_FALSE(History.canUndo());
    CHECK_FALSE(History.canRedo());
    CHECK_FALSE(History.isDirty());

    Edited.Stiffness = 2.0f;
    CHECK(History.commit(Edited));
    Edited.Loop = true;
    CHECK(History.commit(Edited));
    CHECK(History.getStepCount() == 3);

    const anim::Clip* Back = History.undo();
    REQUIRE(Back != nullptr);
    CHECK(Back->Stiffness == 2.0f);
    CHECK_FALSE(Back->Loop);
    Back = History.undo();
    REQUIRE(Back != nullptr);
    CHECK(Back->Stiffness == 1.0f);
    CHECK(History.undo() == nullptr);

    const anim::Clip* Forward = History.redo();
    REQUIRE(Forward != nullptr);
    CHECK(Forward->Stiffness == 2.0f);
    Forward = History.redo();
    REQUIRE(Forward != nullptr);
    CHECK(Forward->Loop);
    CHECK(History.redo() == nullptr);
}

TEST_CASE("A commit of an unchanged clip is not a step", "[editor][history]") {
    ClipHistory History;
    const anim::Clip Edited = makeClip();
    History.reset(Edited);
    CHECK_FALSE(History.commit(Edited));
    CHECK(History.getStepCount() == 1);
}

TEST_CASE("An edit after an undo drops what could be redone", "[editor][history]") {
    ClipHistory History;
    anim::Clip Edited = makeClip();
    History.reset(Edited);
    Edited.Stiffness = 2.0f;
    History.commit(Edited);
    History.undo();
    anim::Clip Other = makeClip();
    Other.Stiffness = 3.0f;
    CHECK(History.commit(Other));
    CHECK_FALSE(History.canRedo());
    CHECK(History.getStepCount() == 2);
    CHECK(History.getCurrent().Stiffness == 3.0f);
}

TEST_CASE("The dirty flag follows the saved state through undo and redo", "[editor][history]") {
    ClipHistory History;
    anim::Clip Edited = makeClip();
    History.reset(Edited);
    Edited.Stiffness = 2.0f;
    History.commit(Edited);
    CHECK(History.isDirty());
    History.undo();
    CHECK_FALSE(History.isDirty());
    History.redo();
    CHECK(History.isDirty());
    History.markSaved();
    CHECK_FALSE(History.isDirty());
    Edited.Loop = true;
    History.commit(Edited);
    CHECK(History.isDirty());
    History.undo();
    CHECK_FALSE(History.isDirty());
    History.undo();
    CHECK(History.isDirty());
}

TEST_CASE("A new unsaved clip is dirty until it is saved", "[editor][history]") {
    ClipHistory History;
    History.resetUnsaved(makeClip());
    CHECK(History.isDirty());
    History.markSaved();
    CHECK_FALSE(History.isDirty());
}

TEST_CASE("Key operations are single steps of the history", "[editor][history]") {
    ClipHistory History;
    anim::Clip Edited = makeClip();
    History.reset(Edited);
    REQUIRE(addKey(Edited, 0.15f).has_value());
    History.commit(Edited);
    REQUIRE(deleteKey(Edited, 2));
    History.commit(Edited);
    CHECK(Edited.Keys.size() == 2);
    CHECK(History.undo()->Keys.size() == 3);
    CHECK(History.undo()->Keys.size() == 2);
}

TEST_CASE("The history is bounded and loses the saved state with the oldest steps", "[editor][history]") {
    ClipHistory History;
    anim::Clip Edited = makeClip();
    History.reset(Edited);
    for (size_t Step = 0; Step < MaxClipHistorySteps + 10; ++Step) {
        Edited.Stiffness = 1.0f + static_cast<float>(Step + 1) * 0.001f;
        History.commit(Edited);
    }
    CHECK(History.getStepCount() == MaxClipHistorySteps);
    CHECK(History.isDirty());
    while (History.undo() != nullptr) {}
    CHECK(History.isDirty());
}

TEST_CASE("Clips are compared in every field", "[editor][history]") {
    const anim::Clip Base = makeClip();
    CHECK(isSameClip(Base, makeClip()));
    anim::Clip Changed = makeClip();
    Changed.Keys[1].Target.setAngle(BodyPart::Torso, 0.5f);
    CHECK_FALSE(isSameClip(Base, Changed));
    Changed = makeClip();
    Changed.BlendInSec = 0.1f;
    CHECK_FALSE(isSameClip(Base, Changed));
    Changed = makeClip();
    Changed.Strikers.set(static_cast<size_t>(BodyPart::ForearmL));
    CHECK_FALSE(isSameClip(Base, Changed));
    Changed = makeClip();
    Changed.PelvisTrack = {{.TimeSec = 0.0f, .OffsetX = 0.0f}};
    CHECK_FALSE(isSameClip(Base, Changed));
}
