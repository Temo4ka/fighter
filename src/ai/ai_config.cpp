#include "ai/ai_config.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::ai {
namespace {

using Json = nlohmann::json;

/// A key of the file and the parameter it sets; Min and Max bound the value.
struct ConfigField {
    std::string_view Key;
    float AiConfig::*Member;
    float Min;
    float Max;
};

constexpr float NoLimit = 1.0e6f;

constexpr std::array ConfigFields = {
    ConfigField{"reactionDelaySec", &AiConfig::ReactionDelaySec, 0.0f, NoLimit},
    ConfigField{"blockChance", &AiConfig::BlockChance, 0.0f, 1.0f},
    ConfigField{"blockHoldSec", &AiConfig::BlockHoldSec, 0.0f, NoLimit},
    ConfigField{"blockRangeM", &AiConfig::BlockRangeM, 0.0f, NoLimit},
    ConfigField{"decisionIntervalSec", &AiConfig::DecisionIntervalSec, 0.0f, NoLimit},
    ConfigField{"attackChance", &AiConfig::AttackChance, 0.0f, 1.0f},
    ConfigField{"pressSec", &AiConfig::PressSec, 0.0f, NoLimit},
    ConfigField{"recoverSec", &AiConfig::RecoverSec, 0.0f, NoLimit},
    ConfigField{"minStaminaFraction", &AiConfig::MinStaminaFraction, 0.0f, 1.0f},
    ConfigField{"rangeMarginM", &AiConfig::RangeMarginM, 0.0f, NoLimit},
};

constexpr std::string_view DefaultRangeKey = "defaultRangeM";
constexpr std::string_view SeedKey = "seed";

} // namespace

AiConfig parseAiConfig(std::string_view JsonText) {
    AiConfig Config;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& [Key, Value] : Root.items()) {
            if (Key == SeedKey) {
                if (!Value.is_number_unsigned()) {
                    throw std::runtime_error(std::format("seed must be a whole number >= 0, not {}", Value.dump()));
                }
                Config.Seed = Value.get<uint64_t>();
            } else if (Key == DefaultRangeKey) {
                if (!Value.is_array() || Value.size() != 2 || !Value[0].is_number() || !Value[1].is_number()) {
                    throw std::runtime_error(std::format("defaultRangeM must be [min, max], not {}", Value.dump()));
                }
                Config.DefaultMinRangeM = Value[0].get<float>();
                Config.DefaultMaxRangeM = Value[1].get<float>();
            } else {
                const auto Found = std::ranges::find(ConfigFields, Key, &ConfigField::Key);
                if (Found == ConfigFields.end()) throw std::runtime_error(std::format("unknown parameter '{}'", Key));
                if (!Value.is_number()) {
                    throw std::runtime_error(std::format("{} must be a number, not {}", Key, Value.dump()));
                }
                const float Number = Value.get<float>();
                if (Number < Found->Min || Number > Found->Max) {
                    throw std::runtime_error(
                        std::format("{} must be in [{}, {}], not {}", Key, Found->Min, Found->Max, Value.dump()));
                }
                Config.*(Found->Member) = Number;
            }
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    if (Config.DefaultMinRangeM < 0.0f || Config.DefaultMaxRangeM < Config.DefaultMinRangeM) {
        throw std::runtime_error(std::format("defaultRangeM must be [min, max] with 0 <= min <= max, not [{}, {}]",
                                             Config.DefaultMinRangeM, Config.DefaultMaxRangeM));
    }
    return Config;
}

AiConfig loadAiConfig(const std::filesystem::path& Path) {
    try {
        return parseAiConfig(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

} // namespace fighter::ai
