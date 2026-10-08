#include "combat/stand_config.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"
#include "stats/validation.hpp"

namespace fighter::combat {
namespace {

using Json = nlohmann::json;

/// A key of the file and the seconds or meters it sets.
struct StandField {
    std::string_view Key;
    float StandConfig::*Member;
    float Min;
    float Max;
};

constexpr std::array StandFields = {
    StandField{"dummy_distance_m", &StandConfig::DummyDistanceM, 0.3f, 20.0f},
    StandField{"settle_sec", &StandConfig::SettleSec, 0.0f, 10.0f},
    StandField{"start_timeout_sec", &StandConfig::StartTimeoutSec, 0.1f, 10.0f},
    StandField{"move_timeout_sec", &StandConfig::MoveTimeoutSec, 0.5f, 30.0f},
    StandField{"repeat_pause_sec", &StandConfig::RepeatPauseSec, 0.0f, 30.0f},
    StandField{"no_dummy_distance_m", &StandConfig::NoDummyDistanceM, 3.0f, 30.0f},
};

stats::Stats readStats(const Json& Node, std::string_view Field, stats::Stats Base);

} // namespace

StandConfig parseStandConfig(std::string_view JsonText) {
    StandConfig Config;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& Field : Root.items()) {
            const std::string& Key = Field.key();
            const Json& Value = Field.value();
            if (Key == "attacker") {
                Config.Attacker = readStats(Value, Key, Config.Attacker);
            } else if (Key == "dummy") {
                Config.Dummy = readStats(Value, Key, Config.Dummy);
            } else if (Key == "use_move_range") {
                if (!Value.is_boolean()) throw std::runtime_error("field 'use_move_range': must be true or false");
                Config.UseMoveRange = Value.get<bool>();
            } else {
                const auto Known = std::ranges::find(StandFields, Key, &StandField::Key);
                if (Known == StandFields.end()) throw std::runtime_error(std::format("unknown field '{}'", Key));
                if (!Value.is_number()) throw std::runtime_error(std::format("field '{}': must be a number", Key));
                const auto Number = Value.get<float>();
                if (!(Number >= Known->Min && Number <= Known->Max)) {
                    throw std::runtime_error(std::format("field '{}': {} is out of [{}, {}]", Key, Number, Known->Min,
                                                         Known->Max));
                }
                Config.*(Known->Member) = Number;
            }
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Config;
}

StandConfig loadStandConfig(const std::filesystem::path& Path) {
    try {
        return parseStandConfig(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

stats::Stats readStats(const Json& Node, std::string_view Field, stats::Stats Base) {
    if (!Node.is_object()) throw std::runtime_error(std::format("field '{}': must be an object", Field));
    for (const auto& Entry : Node.items()) {
        const std::string& Key = Entry.key();
        int* Target = Key == "strength"       ? &Base.Strength
                      : Key == "dexterity"    ? &Base.Dexterity
                      : Key == "constitution" ? &Base.Constitution
                                              : nullptr;
        if (!Target) throw std::runtime_error(std::format("unknown field '{}.{}'", Field, Key));
        if (!Entry.value().is_number_integer()) {
            throw std::runtime_error(std::format("field '{}.{}': must be an integer", Field, Key));
        }
        *Target = Entry.value().get<int>();
    }
    try {
        stats::validateStats(Base);
    } catch (const stats::DataError& Error) {
        throw std::runtime_error(std::format("field '{}': {}", Field, Error.what()));
    }
    return Base;
}

} // namespace

} // namespace fighter::combat
