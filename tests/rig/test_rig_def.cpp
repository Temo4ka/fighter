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
    // Every part of the minimal rig hangs from the pelvis; in the humanoid a
    // shin hangs from a thigh, so a kinematic shin needs a kinematic thigh.
    std::string Humanoid = R"({ "parts": [)";
    std::string Joints;
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        const std::string Name(getBodyPartName(static_cast<BodyPart>(Index)));
        if (Index > 0) Humanoid += ",";
        Humanoid += R"({ "part": ")" + Name + R"(", "shape": "circle", "center": [0, 1], "radius": 0.1 })";
        if (Name == "Pelvis") continue;
        const std::string Parent = Name == "ShinL" ? "ThighL" : "Pelvis";
        if (!Joints.empty()) Joints += ",";
        Joints += R"({ "child": ")" + Name + R"(", "parent": ")" + Parent + R"(", "anchor": [0, 1], "limits": [0, 0] })";
    }
    Humanoid += R"(], "joints": [)" + Joints + R"(], "kinematic": ["ShinL"] })";
    CHECK_THROWS_AS(parseRigDef(Humanoid), std::runtime_error);
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
