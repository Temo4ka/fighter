#include "combat/tuning.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

/// A key of the file and the parameter it sets.
struct TuningField {
    std::string_view Key;
    float CombatTuning::*Member;
};

constexpr std::array TuningFields = {
    TuningField{"spawnDistance", &CombatTuning::SpawnDistance},
    TuningField{"hitSpeedThreshold", &CombatTuning::HitSpeedThreshold},
    TuningField{"bodyHalfWidth", &CombatTuning::BodyHalfWidth},
    TuningField{"separationSpeed", &CombatTuning::SeparationSpeed},
    TuningField{"posedSeparationSpeed", &CombatTuning::PosedSeparationSpeed},
    TuningField{"pushMaxSpeed", &CombatTuning::PushMaxSpeed},
    TuningField{"pushAcceleration", &CombatTuning::PushAcceleration},
    TuningField{"pushSoftOverlap", &CombatTuning::PushSoftOverlap},
    TuningField{"exhaustedSpeedScale", &CombatTuning::ExhaustedSpeedScale},
    TuningField{"exhaustedRecoverFraction", &CombatTuning::ExhaustedRecoverFraction},
    TuningField{"chainWindowSec", &CombatTuning::ChainWindowSec},
    TuningField{"blockBackSpeedScale", &CombatTuning::BlockBackSpeedScale},
    TuningField{"walkStopRate", &CombatTuning::WalkStopRate},
    TuningField{"restMinFootSpread", &CombatTuning::RestMinFootSpread},
    TuningField{"stepMinSpeed", &CombatTuning::StepMinSpeed},
    TuningField{"crouchWalkSpeedScale", &CombatTuning::CrouchWalkSpeedScale},
    TuningField{"crouchStandUpSec", &CombatTuning::CrouchStandUpSec},
    TuningField{"endSettleSec", &CombatTuning::EndSettleSec},
    TuningField{"contactStopDepth", &CombatTuning::ContactStopDepth},
    TuningField{"contactHoldSec", &CombatTuning::ContactHoldSec},
    TuningField{"contactRecoveryBlendSec", &CombatTuning::ContactRecoveryBlendSec},
    TuningField{"contactHertz", &CombatTuning::ContactHertz},
    TuningField{"fighterFriction", &CombatTuning::FighterFriction},
    TuningField{"armOverlapTolerance", &CombatTuning::ArmOverlapTolerance},
    TuningField{"overlapTolerance", &CombatTuning::OverlapTolerance},
};

/// A whole-number key of the file and the parameter it sets.
struct CountField {
    std::string_view Key;
    int CombatTuning::*Member;
};

constexpr std::array CountFields = {
    CountField{"maxChainLength", &CombatTuning::MaxChainLength},
    CountField{"physicsSteps", &CombatTuning::PhysicsSteps},
    CountField{"physicsSubSteps", &CombatTuning::PhysicsSubSteps},
};
constexpr std::string_view BlendsKey = "blends";
constexpr std::string_view LegStepKey = "legStep";

constexpr std::array StanceAfterStopNames = {"mirror", "authored"};

constexpr std::array PoseKindNames = {"stance", "walk", "crouch", "crouchWalk", "block", "strike", "reaction"};
static_assert(PoseKindNames.size() == static_cast<size_t>(PoseKind::Count));

BlendTable parseBlends(const Json& Value);
LegStepTuning parseLegStep(const Json& Value);

} // namespace

std::string_view getStanceAfterStopName(StanceAfterStop Choice) {
    const auto Index = static_cast<size_t>(Choice);
    return Index < StanceAfterStopNames.size() ? StanceAfterStopNames[Index] : "?";
}

std::string_view getPoseKindName(PoseKind Kind) {
    const auto Index = static_cast<size_t>(Kind);
    return Index < PoseKindNames.size() ? PoseKindNames[Index] : "?";
}

float BlendTable::getSec(PoseKind From, PoseKind To) const {
    // The most specific rule wins: both kinds, then the kind blended into,
    // then the kind blended from.
    std::optional<float> ToOnly;
    std::optional<float> FromOnly;
    std::optional<float> AnyToAny;
    for (const Rule& Each : Rules) {
        const bool FromMatches = !Each.From || *Each.From == From;
        const bool ToMatches = !Each.To || *Each.To == To;
        if (!FromMatches || !ToMatches) continue;
        if (Each.From && Each.To) return Each.Sec;
        std::optional<float>& Slot = Each.To ? ToOnly : Each.From ? FromOnly : AnyToAny;
        if (!Slot) Slot = Each.Sec;
    }
    return ToOnly.value_or(FromOnly.value_or(AnyToAny.value_or(DefaultSec)));
}

CombatTuning parseCombatTuning(std::string_view JsonText) {
    CombatTuning Tuning;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& [Key, Value] : Root.items()) {
            if (const auto Count = std::ranges::find(CountFields, Key, &CountField::Key); Count != CountFields.end()) {
                if (!Value.is_number_integer()) {
                    throw std::runtime_error(std::format("{} must be a whole number, not {}", Key, Value.dump()));
                }
                Tuning.*(Count->Member) = Value.get<int>();
                continue;
            }
            if (Key == BlendsKey) {
                Tuning.Blends = parseBlends(Value);
                continue;
            }
            if (Key == LegStepKey) {
                Tuning.LegStep = parseLegStep(Value);
                continue;
            }
            const auto Found = std::ranges::find(TuningFields, Key, &TuningField::Key);
            if (Found == TuningFields.end()) throw std::runtime_error(std::format("unknown parameter '{}'", Key));
            Tuning.*(Found->Member) = Value.get<float>();
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    if (Tuning.SpawnDistance <= 0.0f) throw std::runtime_error("spawnDistance must be positive");
    if (Tuning.BodyHalfWidth <= 0.0f) throw std::runtime_error("bodyHalfWidth must be positive");
    if (Tuning.SeparationSpeed <= 0.0f) throw std::runtime_error("separationSpeed must be positive");
    if (Tuning.PosedSeparationSpeed <= 0.0f) throw std::runtime_error("posedSeparationSpeed must be positive");
    if (Tuning.PushMaxSpeed <= 0.0f) throw std::runtime_error("pushMaxSpeed must be positive");
    if (Tuning.PushAcceleration <= 0.0f) throw std::runtime_error("pushAcceleration must be positive");
    if (Tuning.PushSoftOverlap < 0.0f) throw std::runtime_error("pushSoftOverlap must not be negative");
    if (Tuning.ExhaustedSpeedScale <= 0.0f || Tuning.ExhaustedSpeedScale > 1.0f) {
        throw std::runtime_error("exhaustedSpeedScale must be in (0, 1]");
    }
    if (Tuning.ExhaustedRecoverFraction < 0.0f || Tuning.ExhaustedRecoverFraction > 1.0f) {
        throw std::runtime_error("exhaustedRecoverFraction must be in [0, 1]");
    }
    if (Tuning.ChainWindowSec < 0.0f) throw std::runtime_error("chainWindowSec must not be negative");
    if (Tuning.MaxChainLength < 1) throw std::runtime_error("maxChainLength must be at least 1");
    if (Tuning.BlockBackSpeedScale < 0.0f || Tuning.BlockBackSpeedScale > 1.0f) {
        throw std::runtime_error("blockBackSpeedScale must be in [0, 1]");
    }
    if (Tuning.WalkStopRate <= 0.0f) throw std::runtime_error("walkStopRate must be positive");
    if (Tuning.RestMinFootSpread < 0.0f) throw std::runtime_error("restMinFootSpread must not be negative");
    if (Tuning.StepMinSpeed < 0.0f) throw std::runtime_error("stepMinSpeed must not be negative");
    if (Tuning.CrouchWalkSpeedScale <= 0.0f || Tuning.CrouchWalkSpeedScale > 1.0f) {
        throw std::runtime_error("crouchWalkSpeedScale must be in (0, 1]");
    }
    if (Tuning.CrouchStandUpSec < 0.0f) throw std::runtime_error("crouchStandUpSec must not be negative");
    if (Tuning.EndSettleSec < 0.0f) throw std::runtime_error("endSettleSec must not be negative");
    if (Tuning.ContactStopDepth <= 0.0f) throw std::runtime_error("contactStopDepth must be positive");
    if (Tuning.ContactHoldSec < 0.0f) throw std::runtime_error("contactHoldSec must not be negative");
    if (Tuning.ContactRecoveryBlendSec < 0.0f) {
        throw std::runtime_error("contactRecoveryBlendSec must not be negative");
    }
    if (Tuning.PhysicsSteps < 1) throw std::runtime_error("physicsSteps must be at least 1");
    if (Tuning.PhysicsSubSteps < 1) throw std::runtime_error("physicsSubSteps must be at least 1");
    if (Tuning.ContactHertz <= 0.0f) throw std::runtime_error("contactHertz must be positive");
    if (Tuning.FighterFriction < 0.0f) throw std::runtime_error("fighterFriction must not be negative");
    if (Tuning.ArmOverlapTolerance < 0.0f) throw std::runtime_error("armOverlapTolerance must not be negative");
    if (Tuning.OverlapTolerance < 0.0f) throw std::runtime_error("overlapTolerance must not be negative");
    return Tuning;
}

namespace {

/// "blends": { "default": s, "strikeStartupShare": share, "rules": [{ "from": kind, "to": kind, "sec": s }] },
/// a kind is a PoseKind name or "any".
BlendTable parseBlends(const Json& Value) {
    if (!Value.is_object()) throw std::runtime_error("blends must be an object");
    BlendTable Table;
    const auto getKind = [](const Json& Rule, std::string_view Key, size_t Index) -> std::optional<PoseKind> {
        const std::string Name = Rule.at(std::string(Key)).get<std::string>();
        if (Name == "any") return std::nullopt;
        const auto Found = std::ranges::find(PoseKindNames, Name);
        if (Found == PoseKindNames.end()) {
            throw std::runtime_error(std::format("blends.rules[{}].{}: unknown pose kind '{}'", Index, Key, Name));
        }
        return static_cast<PoseKind>(Found - PoseKindNames.begin());
    };
    for (const auto& [Key, Item] : Value.items()) {
        if (Key == "default") {
            Table.DefaultSec = Item.get<float>();
        } else if (Key == "strikeStartupShare") {
            Table.StrikeStartupShare = Item.get<float>();
        } else if (Key == "rules") {
            if (!Item.is_array()) throw std::runtime_error("blends.rules must be an array");
            for (size_t Index = 0; Index < Item.size(); ++Index) {
                const Json& Rule = Item[Index];
                for (const auto& [RuleKey, Unused] : Rule.items()) {
                    if (RuleKey != "from" && RuleKey != "to" && RuleKey != "sec") {
                        throw std::runtime_error(std::format("blends.rules[{}]: unknown key '{}'", Index, RuleKey));
                    }
                }
                const float Sec = Rule.at("sec").get<float>();
                if (Sec < 0.0f) {
                    throw std::runtime_error(
                        std::format("blends.rules[{}].sec must not be negative, not {}", Index, Sec));
                }
                Table.Rules.push_back(
                    {.From = getKind(Rule, "from", Index), .To = getKind(Rule, "to", Index), .Sec = Sec});
            }
        } else {
            throw std::runtime_error(std::format("blends: unknown key '{}'", Key));
        }
    }
    if (Table.DefaultSec < 0.0f) throw std::runtime_error("blends.default must not be negative");
    if (Table.StrikeStartupShare < 0.0f || Table.StrikeStartupShare > 1.0f) {
        throw std::runtime_error("blends.strikeStartupShare must be in [0, 1]");
    }
    return Table;
}

/// "legStep": { "minDistance": m, "liftHeight": m, "startupShare": share, "sec": s,
/// "stanceAfterStop": "mirror" | "authored" }.
LegStepTuning parseLegStep(const Json& Value) {
    if (!Value.is_object()) throw std::runtime_error("legStep must be an object");
    LegStepTuning Step;
    for (const auto& [Key, Item] : Value.items()) {
        if (Key == "minDistance") {
            Step.MinDistance = Item.get<float>();
        } else if (Key == "liftHeight") {
            Step.LiftHeight = Item.get<float>();
        } else if (Key == "startupShare") {
            Step.StartupShare = Item.get<float>();
        } else if (Key == "sec") {
            Step.Sec = Item.get<float>();
        } else if (Key == "stanceAfterStop") {
            const std::string Name = Item.get<std::string>();
            const auto Found = std::ranges::find(StanceAfterStopNames, Name);
            if (Found == StanceAfterStopNames.end()) {
                throw std::runtime_error(
                    std::format("legStep.stanceAfterStop must be \"mirror\" or \"authored\", not '{}'", Name));
            }
            Step.Stance = static_cast<StanceAfterStop>(Found - StanceAfterStopNames.begin());
        } else {
            throw std::runtime_error(std::format("legStep: unknown key '{}'", Key));
        }
    }
    if (Step.MinDistance < 0.0f) throw std::runtime_error("legStep.minDistance must not be negative");
    if (Step.LiftHeight <= 0.0f) throw std::runtime_error("legStep.liftHeight must be positive");
    if (Step.StartupShare <= 0.0f || Step.StartupShare > 1.0f) {
        throw std::runtime_error("legStep.startupShare must be in (0, 1]");
    }
    if (Step.Sec <= 0.0f) throw std::runtime_error("legStep.sec must be positive");
    return Step;
}

} // namespace

CombatTuning loadCombatTuning(const std::filesystem::path& Path) {
    try {
        return parseCombatTuning(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

} // namespace fighter::combat
