#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "rig/rig_def.hpp"

using namespace fighter;
using namespace fighter::rig;

namespace {

const std::filesystem::path HumanoidPath = std::filesystem::path(FIGHTER_DATA_DIR) / "rigs" / "humanoid.json";

/// A minimal valid rig: every part is a circle, all hang from the pelvis.
/// \p Extra is appended to the root object; a non-empty \p FirstChild
/// replaces the child of the first joint.
std::string makeRigJson(const std::string& Extra = {}, const std::string& FirstChild = {}) {
    std::string Parts;
    std::string Joints;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const std::string Name(getBodyPartName(static_cast<BodyPart>(Index)));
        if (!Parts.empty()) Parts += ",";
        Parts += R"({ "part": ")" + Name + R"(", "shape": "circle", "center": [0, 1], "radius": 0.1 })";
        if (Name == "Pelvis") continue;
        const std::string Child = Joints.empty() && !FirstChild.empty() ? FirstChild : Name;
        if (!Joints.empty()) Joints += ",";
        Joints += R"({ "child": ")" + Child + R"(", "parent": "Pelvis", "anchor": [0, 1], "limits": [-10, 10] })";
    }
    return R"({ "parts": [)" + Parts + R"(], "joints": [)" + Joints + "]" + Extra + "}";
}

} // namespace

TEST_CASE("loadRigDef: the humanoid rig has every part and joint", "[rig]") {
    const RigDef Rig = loadRigDef(HumanoidPath);
    CHECK(Rig.Parts.size() == BodyPartCount);
    CHECK(Rig.Joints.size() == BodyPartCount - 1);
    CHECK(Rig.Root == BodyPart::Pelvis);

    const PartDef& Head = Rig.getPart(BodyPart::Head);
    CHECK(Head.Shape == physics::ShapeKind::Circle);
    CHECK(Head.Radius > 0.0f);
    for (const auto& Joint : Rig.Joints) {
        CHECK(Joint.LowerAngle <= Joint.UpperAngle);
        CHECK(Joint.Strength > 0.0f);
    }
    // The controller parameters come from the file.
    CHECK(Rig.Control.WalkSpeed > 0.0f);
    CHECK(Rig.Control.KnockdownSpeed > 0.0f);
    // The pelvis and the legs are kinematic, the rest is physical.
    CHECK(Rig.Kinematic.test(static_cast<size_t>(BodyPart::Pelvis)));
    CHECK(Rig.Kinematic.test(static_cast<size_t>(BodyPart::FootR)));
    CHECK_FALSE(Rig.Kinematic.test(static_cast<size_t>(BodyPart::Torso)));
}

TEST_CASE("parseRigDef: a minimal rig parses", "[rig]") {
    const RigDef Rig = parseRigDef(makeRigJson(R"(, "control": { "walkSpeed": 2.5 })"));
    CHECK(Rig.Parts.size() == BodyPartCount);
    CHECK(Rig.Control.WalkSpeed == 2.5f);
    // Keys that are not in the file keep their defaults.
    CHECK(Rig.Control.TorqueScale == ControlParams{}.TorqueScale);
}

TEST_CASE("parseRigDef: broken rigs are rejected with a message", "[rig]") {
    CHECK_THROWS_AS(parseRigDef("{ not json"), std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "control": { "walkSpeeed": 2.5 })")), std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "extra": 1)")), std::runtime_error);
    // The first joint attaches Torso instead of Head: Torso gets two parents.
    CHECK_THROWS_AS(parseRigDef(makeRigJson({}, "Torso")), std::runtime_error);

    std::string BadLimits = makeRigJson();
    BadLimits.replace(BadLimits.find("[-10, 10]"), 9, "[10, -10]");
    CHECK_THROWS_AS(parseRigDef(BadLimits), std::runtime_error);

    std::string BadPart = makeRigJson();
    BadPart.replace(BadPart.find("\"Head\""), 6, "\"Neck\"");
    CHECK_THROWS_AS(parseRigDef(BadPart), std::runtime_error);
}

TEST_CASE("parseRigDef: the kinematic parts form a chain from the root", "[rig]") {
    // Without the list only the root is kinematic.
    const RigDef Minimal = parseRigDef(makeRigJson());
    CHECK(Minimal.Kinematic.count() == 1);
    CHECK(Minimal.Kinematic.test(static_cast<size_t>(BodyPart::Pelvis)));

    const RigDef Legs = parseRigDef(makeRigJson(R"(, "kinematic": ["ThighL", "ShinL"])"));
    CHECK(Legs.Kinematic.count() == 3);
    CHECK(Legs.Kinematic.test(static_cast<size_t>(BodyPart::ShinL)));

    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "kinematic": ["Calf"])")), std::runtime_error);
    // A kinematic shin hanging from a physical thigh cannot be posed from the root.
    std::string ShinOnThigh = makeRigJson(R"(, "kinematic": ["ShinL"])");
    const std::string FromPelvis = R"("child": "ShinL", "parent": "Pelvis")";
    ShinOnThigh.replace(ShinOnThigh.find(FromPelvis), FromPelvis.size(), R"("child": "ShinL", "parent": "ThighL")");
    CHECK_THROWS_AS(parseRigDef(ShinOnThigh), std::runtime_error);
    // With the thigh kinematic too, it can.
    ShinOnThigh.replace(ShinOnThigh.find(R"(["ShinL"])"), 9, R"(["ThighL", "ShinL"])");
    CHECK(parseRigDef(ShinOnThigh).Kinematic.count() == 3);
}

TEST_CASE("parseRigDef: durations and speeds must be positive", "[rig]") {
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "control": { "getUpSec": 0 })")), std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "control": { "walkSpeed": -1 })")), std::runtime_error);
    CHECK(parseRigDef(makeRigJson(R"(, "control": { "knockdownSpeed": 2 })")).Control.KnockdownSpeed == 2.0f);
}

TEST_CASE("loadRigDef: a missing file names the path", "[rig]") {
    try {
        loadRigDef("no/such/rig.json");
        FAIL("no exception");
    } catch (const std::runtime_error& Error) {
        CHECK(std::string(Error.what()).find("rig.json") != std::string::npos);
    }
}

TEST_CASE("parseRigDef: parts that pass through or let go of the opponent are physical", "[rig]") {
    const RigDef Arms =
        parseRigDef(makeRigJson(R"(, "passThrough": ["ForearmL"], "unjam": ["UpperArmL", "ForearmL"])"));
    CHECK(Arms.PassThrough.count() == 1);
    CHECK(Arms.PassThrough.test(static_cast<size_t>(BodyPart::ForearmL)));
    CHECK(Arms.Unjam.count() == 2);
    // The root is always kinematic.
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "passThrough": ["Pelvis"])")), std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "kinematic": ["ThighL"], "unjam": ["ThighL"])")),
                    std::runtime_error);

    const RigDef Humanoid = loadRigDef(HumanoidPath);
    // The arms collide: a jab hits the raised forearm (the user's decision).
    CHECK(Humanoid.PassThrough.none());
    CHECK(Humanoid.Unjam.test(static_cast<size_t>(BodyPart::UpperArmL)));
}

TEST_CASE("parseRigDef: the weapon mount needs a capsule", "[rig]") {
    const RigDef Humanoid = loadRigDef(HumanoidPath);
    CHECK(Humanoid.Weapon.Part == BodyPart::ForearmR);
    CHECK(Humanoid.Weapon.Radius > 0.0f);
    // Without a "weapon" object the default mount is kept, whatever the part.
    CHECK(parseRigDef(makeRigJson()).Weapon.Part == BodyPart::ForearmR);
    // The minimal rig is made of circles: no weapon can continue them.
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "weapon": { "part": "ForearmL" })")), std::runtime_error);
    std::string Capsule = makeRigJson(R"(, "weapon": { "part": "ForearmL", "angle": 90, "radius": 0.03 })");
    const std::string Circle = R"({ "part": "ForearmL", "shape": "circle", "center": [0, 1], "radius": 0.1 })";
    Capsule.replace(Capsule.find(Circle), Circle.size(),
                    R"({ "part": "ForearmL", "shape": "capsule", "from": [0, 1], "to": [0, 0.8], "radius": 0.04 })");
    const RigDef Armed = parseRigDef(Capsule);
    CHECK(Armed.Weapon.Part == BodyPart::ForearmL);
    CHECK(Armed.Weapon.Angle == Catch::Approx(1.5708f));
    CHECK(Armed.Weapon.Radius == 0.03f);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "weapon": { "part": "ForearmL", "length": 1 })")),
                    std::runtime_error);
}

TEST_CASE("parseRigDef: distances of the body of task 2.1 must not be negative", "[rig]") {
    for (const auto* Key : {"closeRange", "lyingClearance", "footLockSlip", "footRestepDistance", "jamSec"}) {
        CAPTURE(Key);
        const std::string Control = std::string(R"(, "control": { ")") + Key + R"(": -0.1 })";
        CHECK_THROWS_AS(parseRigDef(makeRigJson(Control)), std::runtime_error);
    }
    CHECK(parseRigDef(makeRigJson(R"(, "control": { "closeRange": 0.9 })")).Control.CloseRange == 0.9f);
}
