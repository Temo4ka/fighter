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

TEST_CASE("parseRigDef: the yield pose sets angles of parts that yield", "[rig]") {
    const RigDef Arms = parseRigDef(
        makeRigJson(R"(, "unjam": ["UpperArmL", "ForearmL"], "yieldPose": { "ForearmL": 90 },)"
                    R"( "control": { "yieldStiffness": 0.2, "yieldSec": 0.4 })"));
    CHECK(Arms.YieldPosed.count() == 1);
    CHECK(Arms.YieldPosed.test(static_cast<size_t>(BodyPart::ForearmL)));
    CHECK(Arms.YieldAngles[static_cast<size_t>(BodyPart::ForearmL)] == Catch::Approx(1.5708f));
    CHECK(Arms.Control.YieldStiffness == 0.2f);
    CHECK(Arms.Control.YieldSec == 0.4f);
    // Only a part that yields has a yield pose; names are checked.
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "yieldPose": { "ForearmL": 90 })")), std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "unjam": ["ForearmL"], "yieldPose": { "Forearm": 90 })")),
                    std::runtime_error);
    CHECK_THROWS_AS(parseRigDef(makeRigJson(R"(, "control": { "yieldSec": -1 })")), std::runtime_error);

    // The humanoid tucks both arms: the elbows bend.
    const RigDef Humanoid = loadRigDef(HumanoidPath);
    for (const auto Part : {BodyPart::UpperArmL, BodyPart::ForearmL, BodyPart::UpperArmR, BodyPart::ForearmR}) {
        CHECK(Humanoid.YieldPosed.test(static_cast<size_t>(Part)));
    }
    CHECK(Humanoid.YieldAngles[static_cast<size_t>(BodyPart::ForearmL)] > 2.0f);
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

TEST_CASE("parseRigDef: the smooth body parameters are read and checked", "[rig]") {
    const RigDef Rig = parseRigDef(makeRigJson(
        R"(, "control": { "carrierTransfer": 0.8, "knockbackTransfer": 0.2, "feedForward": 0.5,)"
        R"( "gravityCompensation": 0.7, "holdGravityMargin": 2, "dampedErrorAngle": 0.4,)"
        R"( "yieldReturnClearance": 0.06, "knockdownLegStiffness": 0.3 })"));
    CHECK(Rig.Control.CarrierTransfer == 0.8f);
    CHECK(Rig.Control.KnockbackTransfer == 0.2f);
    CHECK(Rig.Control.FeedForward == 0.5f);
    CHECK(Rig.Control.GravityCompensation == 0.7f);
    CHECK(Rig.Control.HoldGravityMargin == 2.0f);
    CHECK(Rig.Control.DampedErrorAngle == 0.4f);
    CHECK(Rig.Control.YieldReturnClearance == 0.06f);
    CHECK(Rig.Control.KnockdownLegStiffness == 0.3f);

    // Shares lie in [0, 1]; margins and distances are not negative.
    for (const auto* Key : {"carrierTransfer", "knockbackTransfer", "feedForward", "gravityCompensation"}) {
        CAPTURE(Key);
        CHECK_THROWS_AS(parseRigDef(makeRigJson(std::string(R"(, "control": { ")") + Key + R"(": 1.5 })")),
                        std::runtime_error);
        CHECK_THROWS_AS(parseRigDef(makeRigJson(std::string(R"(, "control": { ")") + Key + R"(": -0.1 })")),
                        std::runtime_error);
    }
    for (const auto* Key : {"holdGravityMargin", "dampedErrorAngle", "yieldReturnClearance", "knockdownLegStiffness"}) {
        CAPTURE(Key);
        CHECK_THROWS_AS(parseRigDef(makeRigJson(std::string(R"(, "control": { ")") + Key + R"(": -0.1 })")),
                        std::runtime_error);
    }
}
