#include "combat/tuning.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>

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
    TuningField{"exhaustedSpeedScale", &CombatTuning::ExhaustedSpeedScale},
    TuningField{"exhaustedRecoverFraction", &CombatTuning::ExhaustedRecoverFraction},
    TuningField{"chainWindowSec", &CombatTuning::ChainWindowSec},
    TuningField{"blockBackSpeedScale", &CombatTuning::BlockBackSpeedScale},
    TuningField{"walkStopRate", &CombatTuning::WalkStopRate},
    TuningField{"stanceSettleSec", &CombatTuning::StanceSettleSec},
    TuningField{"switchStepShare", &CombatTuning::SwitchStepShare},
    TuningField{"crouchWalkSpeedScale", &CombatTuning::CrouchWalkSpeedScale},
    TuningField{"crouchStandUpSec", &CombatTuning::CrouchStandUpSec},
    TuningField{"endSettleSec", &CombatTuning::EndSettleSec},
    TuningField{"contactStopDepth", &CombatTuning::ContactStopDepth},
    TuningField{"contactHoldSec", &CombatTuning::ContactHoldSec},
    TuningField{"contactRecoveryBlendSec", &CombatTuning::ContactRecoveryBlendSec},
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
/// The yes/no parameters.
constexpr std::string_view StopSlidesFeetKey = "stopSlidesFeet";

} // namespace

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
            if (Key == StopSlidesFeetKey) {
                if (!Value.is_boolean()) {
                    throw std::runtime_error(std::format("{} must be true or false, not {}", Key, Value.dump()));
                }
                Tuning.StopSlidesFeet = Value.get<bool>();
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
    if (Tuning.StanceSettleSec < 0.0f) throw std::runtime_error("stanceSettleSec must not be negative");
    if (Tuning.SwitchStepShare <= 0.0f || Tuning.SwitchStepShare > 1.0f) {
        throw std::runtime_error("switchStepShare must be in (0, 1]");
    }
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
    if (Tuning.ArmOverlapTolerance < 0.0f) throw std::runtime_error("armOverlapTolerance must not be negative");
    if (Tuning.OverlapTolerance < 0.0f) throw std::runtime_error("overlapTolerance must not be negative");
    return Tuning;
}

CombatTuning loadCombatTuning(const std::filesystem::path& Path) {
    try {
        return parseCombatTuning(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

} // namespace fighter::combat
