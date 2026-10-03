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
    TuningField{"exhaustedSpeedScale", &CombatTuning::ExhaustedSpeedScale},
};

} // namespace

CombatTuning parseCombatTuning(std::string_view JsonText) {
    CombatTuning Tuning;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& [Key, Value] : Root.items()) {
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
    if (Tuning.ExhaustedSpeedScale <= 0.0f || Tuning.ExhaustedSpeedScale > 1.0f) {
        throw std::runtime_error("exhaustedSpeedScale must be in (0, 1]");
    }
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
