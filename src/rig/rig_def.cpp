#include "rig/rig_def.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::rig {
namespace {

using Json = nlohmann::json;

constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
/// Box2D accepts joint limits within +-0.99 pi.
constexpr float MaxLimitDegrees = 175.0f;

/// A key of the "control" object and the parameter it sets.
struct ControlField {
    std::string_view Key;
    float ControlParams::*Member;
};

constexpr std::array ControlFields = {
    ControlField{"torqueScale", &ControlParams::TorqueScale},
    ControlField{"gainScale", &ControlParams::GainScale},
    ControlField{"maxJointSpeed", &ControlParams::MaxJointSpeed},
    ControlField{"angularDamping", &ControlParams::AngularDamping},
    ControlField{"carrierTransfer", &ControlParams::CarrierTransfer},
    ControlField{"knockbackTransfer", &ControlParams::KnockbackTransfer},
    ControlField{"feedForward", &ControlParams::FeedForward},
    ControlField{"gravityCompensation", &ControlParams::GravityCompensation},
    ControlField{"holdGravityMargin", &ControlParams::HoldGravityMargin},
    ControlField{"dampedErrorAngle", &ControlParams::DampedErrorAngle},
    ControlField{"walkSpeed", &ControlParams::WalkSpeed},
    ControlField{"backwardSpeedScale", &ControlParams::BackwardSpeedScale},
    ControlField{"walkAcceleration", &ControlParams::WalkAcceleration},
    ControlField{"walkDeceleration", &ControlParams::WalkDeceleration},
    ControlField{"minStiffness", &ControlParams::MinStiffness},
    ControlField{"stiffnessPerImpulse", &ControlParams::StiffnessPerImpulse},
    ControlField{"stiffnessRecovery", &ControlParams::StiffnessRecovery},
    ControlField{"knockbackScale", &ControlParams::KnockbackScale},
    ControlField{"knockbackDecay", &ControlParams::KnockbackDecay},
    ControlField{"knockdownSpeed", &ControlParams::KnockdownSpeed},
    ControlField{"knockdownSec", &ControlParams::KnockdownSec},
    ControlField{"getUpSec", &ControlParams::GetUpSec},
    ControlField{"knockdownStiffness", &ControlParams::KnockdownStiffness},
    ControlField{"knockdownSpin", &ControlParams::KnockdownSpin},
    ControlField{"knockoutStiffness", &ControlParams::KnockoutStiffness},
    ControlField{"knockdownLegStiffness", &ControlParams::KnockdownLegStiffness},
    ControlField{"closeRange", &ControlParams::CloseRange},
    ControlField{"lyingClearance", &ControlParams::LyingClearance},
    ControlField{"wallTouchDistance", &ControlParams::WallTouchDistance},
    ControlField{"footPlantHeight", &ControlParams::FootPlantHeight},
    ControlField{"footLockSlip", &ControlParams::FootLockSlip},
    ControlField{"footLockRelease", &ControlParams::FootLockRelease},
    ControlField{"footSlideSpeed", &ControlParams::FootSlideSpeed},
    ControlField{"footRestepDistance", &ControlParams::FootRestepDistance},
    ControlField{"footStepLift", &ControlParams::FootStepLift},
    ControlField{"jamAngle", &ControlParams::JamAngle},
    ControlField{"jamSec", &ControlParams::JamSec},
    ControlField{"yieldStiffness", &ControlParams::YieldStiffness},
    ControlField{"yieldSec", &ControlParams::YieldSec},
    ControlField{"yieldReturnClearance", &ControlParams::YieldReturnClearance},
};

/// Parameters that must not be negative: distances and durations.
constexpr std::array<std::string_view, 21> NonNegativeFields = {
    "closeRange",      "lyingClearance",     "wallTouchDistance", "footPlantHeight",     "footLockSlip",
    "footLockRelease", "footRestepDistance", "footStepLift",      "jamAngle",            "jamSec",
    "knockdownSpin",   "knockoutStiffness",  "knockdownSec",      "knockbackDecay",      "minStiffness",
    "yieldStiffness",  "yieldSec",           "holdGravityMargin", "dampedErrorAngle",    "yieldReturnClearance",
    "knockdownLegStiffness"};

/// Parameters that are shares: from 0 to 1.
constexpr std::array<std::string_view, 4> ShareFields = {"carrierTransfer", "knockbackTransfer", "feedForward",
                                                         "gravityCompensation"};

/// Parameters that must be positive: they divide or set a duration.
constexpr std::array<std::string_view, 6> PositiveFields = {"walkSpeed",      "walkAcceleration", "walkDeceleration",
                                                            "knockdownSpeed", "getUpSec",         "footSlideSpeed"};

void checkKeys(const Json& Node, std::initializer_list<std::string_view> Known, std::string_view Where);
PartDef parsePart(const Json& Node);
JointDef parseJoint(const Json& Node);
ControlParams parseControl(const Json& Node);
WeaponMount parseWeapon(const Json& Node);
std::bitset<BodyPartCount> parsePartSet(const Json& Root, const char* Key);
void parseYieldPose(const Json& Node, RigDef& Def);
BodyPart parseBodyPart(const Json& Node);
Vec2 parseVec2(const Json& Node);
void validateRig(const RigDef& Def);

} // namespace

const PartDef& RigDef::getPart(BodyPart Part) const {
    const auto Found = std::ranges::find(Parts, Part, &PartDef::Part);
    if (Found == Parts.end()) throw std::runtime_error(std::format("rig has no part {}", getBodyPartName(Part)));
    return *Found;
}

RigDef parseRigDef(std::string_view JsonText) {
    RigDef Result;
    bool HasWeapon = false;
    try {
        const Json Root = Json::parse(JsonText);
        checkKeys(Root,
                  {"root", "parts", "joints", "kinematic", "passThrough", "unjam", "yieldPose", "weapon", "control"},
                  "rig");
        if (const auto RootPart = Root.find("root"); RootPart != Root.end()) Result.Root = parseBodyPart(*RootPart);
        for (const auto& Part : Root.at("parts")) Result.Parts.push_back(parsePart(Part));
        for (const auto& Joint : Root.at("joints")) Result.Joints.push_back(parseJoint(Joint));
        // The root is always kinematic: the pelvis controller moves it.
        Result.Kinematic.set(static_cast<size_t>(Result.Root));
        Result.Kinematic |= parsePartSet(Root, "kinematic");
        Result.PassThrough = parsePartSet(Root, "passThrough");
        Result.Unjam = parsePartSet(Root, "unjam");
        if (const auto Yield = Root.find("yieldPose"); Yield != Root.end()) parseYieldPose(*Yield, Result);
        if (const auto Weapon = Root.find("weapon"); Weapon != Root.end()) {
            Result.Weapon = parseWeapon(*Weapon);
            HasWeapon = true;
        }
        if (const auto Control = Root.find("control"); Control != Root.end()) Result.Control = parseControl(*Control);
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    validateRig(Result);
    // The weapon continues a capsule from its far end. Without a "weapon"
    // object the default part is used if it is a capsule, else none.
    if (HasWeapon && Result.getPart(Result.Weapon.Part).Shape != physics::ShapeKind::Capsule) {
        throw std::runtime_error(
            std::format("weapon: part {} must be a capsule", getBodyPartName(Result.Weapon.Part)));
    }
    return Result;
}

RigDef loadRigDef(const std::filesystem::path& Path) {
    try {
        return parseRigDef(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

/// Throws if \p Node has a key that is not in \p Known: a typo in a data
/// file must not be silently ignored during live tuning.
void checkKeys(const Json& Node, std::initializer_list<std::string_view> Known, std::string_view Where) {
    if (!Node.is_object()) throw std::runtime_error(std::format("{}: expected a JSON object", Where));
    for (const auto& Item : Node.items()) {
        if (std::ranges::find(Known, Item.key()) == Known.end()) {
            throw std::runtime_error(std::format("{}: unknown key '{}'", Where, Item.key()));
        }
    }
}

PartDef parsePart(const Json& Node) {
    PartDef Part;
    Part.Part = parseBodyPart(Node.at("part"));
    checkKeys(Node, {"part", "shape", "from", "to", "center", "halfExtents", "radius", "friction"},
              std::format("part {}", getBodyPartName(Part.Part)));
    Part.Radius = Node.value("radius", 0.0f);
    Part.Friction = Node.value("friction", Part.Friction);

    const std::string Shape = Node.at("shape").get<std::string>();
    if (Shape == "capsule") {
        Part.Shape = physics::ShapeKind::Capsule;
        Part.Begin = parseVec2(Node.at("from"));
        Part.End = parseVec2(Node.at("to"));
    } else if (Shape == "circle") {
        Part.Shape = physics::ShapeKind::Circle;
        Part.Center = parseVec2(Node.at("center"));
    } else if (Shape == "box") {
        Part.Shape = physics::ShapeKind::Box;
        Part.Center = parseVec2(Node.at("center"));
        Part.HalfExtents = parseVec2(Node.at("halfExtents"));
    } else {
        throw std::runtime_error(std::format("part {}: unknown shape '{}'", getBodyPartName(Part.Part), Shape));
    }
    return Part;
}

JointDef parseJoint(const Json& Node) {
    JointDef Joint;
    Joint.Child = parseBodyPart(Node.at("child"));
    checkKeys(Node, {"child", "parent", "anchor", "limits", "strength"},
              std::format("joint {}", getBodyPartName(Joint.Child)));
    Joint.Parent = parseBodyPart(Node.at("parent"));
    Joint.Anchor = parseVec2(Node.at("anchor"));
    const Json& Limits = Node.at("limits");
    const float Lower = Limits.at(0).get<float>();
    const float Upper = Limits.at(1).get<float>();
    if (Lower > Upper || Lower < -MaxLimitDegrees || Upper > MaxLimitDegrees) {
        throw std::runtime_error(std::format("joint {}: limits must be ordered and within +-{} degrees",
                                             getBodyPartName(Joint.Child), MaxLimitDegrees));
    }
    Joint.LowerAngle = Lower * RadiansPerDegree;
    Joint.UpperAngle = Upper * RadiansPerDegree;
    Joint.Strength = Node.value("strength", 1.0f);
    return Joint;
}

ControlParams parseControl(const Json& Node) {
    // Every key is optional (the default stays), but an unknown key is an
    // error: a typo during live tuning must not be silently ignored.
    ControlParams Params;
    for (const auto& [Key, Value] : Node.items()) {
        const auto Found = std::ranges::find(ControlFields, Key, &ControlField::Key);
        if (Found == ControlFields.end()) {
            throw std::runtime_error(std::format("control: unknown parameter '{}'", Key));
        }
        Params.*(Found->Member) = Value.get<float>();
        if (std::ranges::find(PositiveFields, Key) != PositiveFields.end() && Params.*(Found->Member) <= 0.0f) {
            throw std::runtime_error(std::format("control: '{}' must be positive", Key));
        }
        if (std::ranges::find(NonNegativeFields, Key) != NonNegativeFields.end() && Params.*(Found->Member) < 0.0f) {
            throw std::runtime_error(std::format("control: '{}' must not be negative", Key));
        }
        const float Share = Params.*(Found->Member);
        if (std::ranges::find(ShareFields, Key) != ShareFields.end() && (Share < 0.0f || Share > 1.0f)) {
            throw std::runtime_error(std::format("control: '{}' must be from 0 to 1, got {}", Key, Share));
        }
    }
    return Params;
}

WeaponMount parseWeapon(const Json& Node) {
    checkKeys(Node, {"part", "angle", "radius"}, "weapon");
    WeaponMount Mount;
    if (const auto Part = Node.find("part"); Part != Node.end()) Mount.Part = parseBodyPart(*Part);
    Mount.Angle = Node.value("angle", 0.0f) * RadiansPerDegree;
    Mount.Radius = Node.value("radius", Mount.Radius);
    if (Mount.Radius <= 0.0f) throw std::runtime_error("weapon: 'radius' must be positive");
    return Mount;
}

/// A list of body parts under \p Key of the rig object; none if it is absent.
std::bitset<BodyPartCount> parsePartSet(const Json& Root, const char* Key) {
    std::bitset<BodyPartCount> Result;
    for (const auto& Part : Root.value(Key, Json::array())) Result.set(static_cast<size_t>(parseBodyPart(Part)));
    return Result;
}

/// The "yieldPose" object: body part -> joint angle in degrees.
void parseYieldPose(const Json& Node, RigDef& Def) {
    if (!Node.is_object()) throw std::runtime_error("yieldPose: expected a JSON object");
    for (const auto& [Name, Angle] : Node.items()) {
        const auto Part = findBodyPart(Name);
        if (!Part) throw std::runtime_error(std::format("yieldPose: unknown body part '{}'", Name));
        const auto Index = static_cast<size_t>(*Part);
        Def.YieldAngles[Index] = Angle.get<float>() * RadiansPerDegree;
        Def.YieldPosed.set(Index);
    }
}

BodyPart parseBodyPart(const Json& Node) {
    const std::string Name = Node.get<std::string>();
    const auto Part = findBodyPart(Name);
    if (!Part) throw std::runtime_error(std::format("unknown body part '{}'", Name));
    return *Part;
}

Vec2 parseVec2(const Json& Node) { return {Node.at(0).get<float>(), Node.at(1).get<float>()}; }

void validateRig(const RigDef& Def) {
    std::bitset<BodyPartCount> Defined;
    for (const auto& Part : Def.Parts) {
        const auto Index = static_cast<size_t>(Part.Part);
        if (Defined.test(Index)) {
            throw std::runtime_error(std::format("part {} is defined twice", getBodyPartName(Part.Part)));
        }
        Defined.set(Index);
    }
    if (!Defined.all()) throw std::runtime_error("every body part must be defined");

    // Walking the joints in order must reach every part from the root, with
    // each parent placed before its children.
    std::bitset<BodyPartCount> Reached;
    Reached.set(static_cast<size_t>(Def.Root));
    for (const auto& Joint : Def.Joints) {
        if (!Reached.test(static_cast<size_t>(Joint.Parent))) {
            throw std::runtime_error(std::format("joint {}: parent {} is not attached yet",
                                                 getBodyPartName(Joint.Child), getBodyPartName(Joint.Parent)));
        }
        if (Reached.test(static_cast<size_t>(Joint.Child))) {
            throw std::runtime_error(std::format("part {} has two parents", getBodyPartName(Joint.Child)));
        }
        Reached.set(static_cast<size_t>(Joint.Child));
    }
    if (!Reached.all()) throw std::runtime_error("every body part except the root needs a joint");

    // Lists of physical parts: a posed part is moved by code, it cannot pass
    // through or let go.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (!Def.Kinematic.test(Index)) continue;
        const std::string_view Name = getBodyPartName(static_cast<BodyPart>(Index));
        if (Def.PassThrough.test(Index)) throw std::runtime_error(std::format("passThrough: {} is kinematic", Name));
        if (Def.Unjam.test(Index)) throw std::runtime_error(std::format("unjam: {} is kinematic", Name));
    }
    // Only a limb that yields pulls back to the yield pose.
    for (size_t Index = 0; Index < BodyPartCount; ++Index) {
        if (Def.YieldPosed.test(Index) && !Def.Unjam.test(Index)) {
            throw std::runtime_error(
                std::format("yieldPose: {} is not in the unjam list", getBodyPartName(static_cast<BodyPart>(Index))));
        }
    }

    // Kinematic parts are posed by forward kinematics from the root, so each
    // of them hangs from another kinematic part.
    for (const auto& Joint : Def.Joints) {
        const bool ChildKinematic = Def.Kinematic.test(static_cast<size_t>(Joint.Child));
        if (ChildKinematic && !Def.Kinematic.test(static_cast<size_t>(Joint.Parent))) {
            throw std::runtime_error(std::format("kinematic part {} hangs from the physical part {}",
                                                 getBodyPartName(Joint.Child), getBodyPartName(Joint.Parent)));
        }
    }
}

} // namespace

} // namespace fighter::rig
