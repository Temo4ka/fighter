#include "ui/ui_config.hpp"

#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace fighter::ui {

UiConfig parseUiConfig(std::string_view Text, std::string_view SourceName) {
    const std::string Source(SourceName);
    nlohmann::json Root;
    try {
        Root = nlohmann::json::parse(Text);
    } catch (const nlohmann::json::exception& Error) {
        throw std::runtime_error(std::format("{}: {}", Source, Error.what()));
    }
    if (!Root.is_object()) throw std::runtime_error(std::format("{}: the root must be an object", Source));

    UiConfig Config;
    for (const auto& [Key, Value] : Root.items()) {
        if (Key != "results_delay_sec") throw std::runtime_error(std::format("{}: unknown key \"{}\"", Source, Key));
        if (!Value.is_number() || !std::isfinite(Value.get<double>()) || Value.get<double>() < 0.0 ||
            Value.get<double>() > 60.0) {
            throw std::runtime_error(
                std::format("{}: results_delay_sec must be a number in [0, 60], got {}", Source, Value.dump()));
        }
        Config.ResultsDelaySec = Value.get<double>();
    }
    return Config;
}

UiConfig loadUiConfig(const std::filesystem::path& Path) {
    std::ifstream File(Path);
    if (!File) return {};
    std::ostringstream Text;
    Text << File.rdbuf();
    return parseUiConfig(Text.str(), Path.string());
}

} // namespace fighter::ui
