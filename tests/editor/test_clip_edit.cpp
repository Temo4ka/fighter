#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "anim/clip.hpp"
#include "editor/clip_edit.hpp"
#include "editor/clip_writer.hpp"

using namespace fighter;
using namespace fighter::editor;
using Catch::Approx;

namespace {

/// A one-shot clip with keys at 0, 0.2 and 0.4 (torso 0, 10, 20 degrees), a
/// pelvis track and an active phase, 0.5 s long.
anim::Clip makeClip() {
    return anim::parseClip(R"({
      "duration": 0.5, "active": [0.2, 0.3], "strikers": ["ForearmL"],
      "pelvisX": [ { "t": 0, "x": 0 }, { "t": 0.4, "x": 0.2 } ],
      "keys": [
        { "t": 0.0, "pose": { "Torso": 0, "ForearmL": 100 } },
        { "t": 0.2, "pose": { "Torso": 10, "ForearmL": 50 } },
        { "t": 0.4, "pose": { "Torso": 20, "ForearmL": 0 } }
      ] })",
                           "edit");
}

void checkValid(const anim::Clip& Edited) { CHECK(findClipProblem(Edited) == ""); }

} // namespace

TEST_CASE("A new key keeps the motion the clip already has", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    const anim::Pose Before = anim::sampleClip(Edited, 0.1f);
    const auto Added = addKey(Edited, 0.1f);
    REQUIRE(Added.has_value());
    CHECK(*Added == 1);
    REQUIRE(Edited.Keys.size() == 4);
    CHECK(Edited.Keys[1].TimeSec == Approx(0.1f));
    CHECK(Edited.Keys[1].Target.getAngle(BodyPart::Torso) == Approx(Before.getAngle(BodyPart::Torso)));
    const anim::Clip Original = makeClip();
    for (const float Time : {0.05f, 0.1f, 0.15f, 0.3f}) {
        CHECK(anim::sampleClip(Edited, Time).getAngle(BodyPart::ForearmL) ==
              Approx(anim::sampleClip(Original, Time).getAngle(BodyPart::ForearmL)));
    }
    checkValid(Edited);
}

TEST_CASE("A key cannot be added at 0, past the end or on top of another", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    CHECK_FALSE(addKey(Edited, 0.0f));
    CHECK_FALSE(addKey(Edited, 0.51f));
    CHECK_FALSE(addKey(Edited, 0.205f));
    CHECK(Edited.Keys.size() == 3);
    CHECK(addKey(Edited, 0.5f).has_value());   // the end itself is fine
    checkValid(Edited);
}

TEST_CASE("The first key and the last key left stay", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    CHECK_FALSE(deleteKey(Edited, 0));
    CHECK_FALSE(deleteKey(Edited, 9));
    CHECK(deleteKey(Edited, 2));
    CHECK(deleteKey(Edited, 1));
    CHECK(Edited.Keys.size() == 1);
    CHECK_FALSE(deleteKey(Edited, 0));
    checkValid(Edited);
}

TEST_CASE("A duplicate goes halfway to the next key", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    const auto Copy = duplicateKey(Edited, 1);
    REQUIRE(Copy.has_value());
    CHECK(*Copy == 2);
    CHECK(Edited.Keys[2].TimeSec == Approx(0.3f));
    CHECK(Edited.Keys[2].Target.getAngle(BodyPart::Torso) == Approx(Edited.Keys[1].Target.getAngle(BodyPart::Torso)));
    CHECK(Edited.Keys.size() == 4);
    checkValid(Edited);
}

TEST_CASE("A duplicate of the last key goes after it but not past the end", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    const auto Copy = duplicateKey(Edited, 2);
    REQUIRE(Copy.has_value());
    CHECK(Edited.Keys[3].TimeSec == Approx(0.5f));
    // No room for another one.
    CHECK_FALSE(duplicateKey(Edited, 3));
    // And none between keys 0.01 apart.
    anim::Clip Tight = makeClip();
    Tight.Keys[1].TimeSec = 0.01f;
    CHECK_FALSE(duplicateKey(Tight, 0));
    checkValid(Edited);
}

TEST_CASE("A key moves only between its neighbours", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    CHECK(moveKey(Edited, 1, 0.3f) == Approx(0.3f));
    CHECK(moveKey(Edited, 1, 0.9f) == Approx(0.4f));   // not past the next key
    CHECK(moveKey(Edited, 1, -1.0f) == Approx(0.0f));  // not before the first
    CHECK(moveKey(Edited, 2, 5.0f) == Approx(0.5f));   // the last one stops at the end
    CHECK(moveKey(Edited, 0, 0.1f) == Approx(0.0f));   // the first one stays at 0
    CHECK(Edited.Keys.front().TimeSec == 0.0f);
    checkValid(Edited);
}

TEST_CASE("The duration does not go below what the clip uses", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    CHECK(setDuration(Edited, 0.2f) == Approx(0.4f));   // the last key and the pelvis track end at 0.4
    CHECK(setDuration(Edited, 0.8f) == Approx(0.8f));
    CHECK(Edited.DurationSec == Approx(0.8f));
    checkValid(Edited);
}

TEST_CASE("The active phase stays inside the clip", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    setActive(Edited, 0.3f, 0.1f);
    CHECK(Edited.ActiveBeginSec == Approx(0.3f));
    CHECK(Edited.ActiveEndSec == Approx(0.3f));
    setActive(Edited, -1.0f, 9.0f);
    CHECK(Edited.ActiveBeginSec == Approx(0.0f));
    CHECK(Edited.ActiveEndSec == Approx(0.5f));
    checkValid(Edited);
}

TEST_CASE("Pelvis keys are added with the offset the track has there", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    const auto Added = addPelvisKey(Edited, 0.2f);
    REQUIRE(Added.has_value());
    CHECK(*Added == 1);
    CHECK(Edited.PelvisTrack[1].OffsetX == Approx(0.1f));
    CHECK_FALSE(addPelvisKey(Edited, 0.2f));
    checkValid(Edited);

    setPelvisKey(Edited, 1, 0.25f, 5.0f);
    CHECK(Edited.PelvisTrack[1].TimeSec == Approx(0.25f));
    CHECK(Edited.PelvisTrack[1].OffsetX == Approx(anim::MaxPelvisOffsetM));
    setPelvisKey(Edited, 1, 0.9f, 0.0f);
    CHECK(Edited.PelvisTrack[1].TimeSec == Approx(0.4f));   // not past the next key
    setPelvisKey(Edited, 0, 0.1f, 0.5f);
    CHECK(Edited.PelvisTrack[0].TimeSec == 0.0f);           // the first key is fixed
    CHECK(Edited.PelvisTrack[0].OffsetX == 0.0f);
    checkValid(Edited);
}

TEST_CASE("The pelvis track is created on demand and dropped when emptied", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    Edited.PelvisTrack.clear();
    const auto Added = addPelvisKey(Edited, 0.3f);
    REQUIRE(Added.has_value());
    CHECK(Edited.PelvisTrack.size() == 2);
    CHECK(Edited.PelvisTrack.front().TimeSec == 0.0f);
    checkValid(Edited);

    CHECK_FALSE(deletePelvisKey(Edited, 0));
    CHECK(deletePelvisKey(Edited, 1));
    CHECK(Edited.PelvisTrack.empty());
    checkValid(Edited);
}

TEST_CASE("A looping clip has no pelvis track to add to", "[editor][edit]") {
    anim::Clip Edited = makeClip();
    Edited.PelvisTrack.clear();
    Edited.Loop = true;
    CHECK_FALSE(addPelvisKey(Edited, 0.2f));
    CHECK(Edited.PelvisTrack.empty());
}
