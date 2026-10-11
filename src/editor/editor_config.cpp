#include "editor/editor_config.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::editor {
namespace {

using Json = nlohmann::json;

/// A key of the file and the number it sets.
struct ConfigField {
    std::string_view Key;
    float EditorConfig::*Member;
    float Min;
    float Max;
};

constexpr std::array ConfigFields = {
    ConfigField{"tip_floor_margin_m", &EditorConfig::TipFloorMarginM, 0.0f, 1.0f},
    ConfigField{"limit_margin_deg", &EditorConfig::LimitMarginDeg, 0.0f, 45.0f},
    ConfigField{"divergence_deg", &EditorConfig::DivergenceDeg, 1.0f, 180.0f},
    ConfigField{"trajectory_step_sec", &EditorConfig::TrajectoryStepSec, 0.004f, 0.25f},
};

} // namespace

EditorConfig parseEditorConfig(std::string_view JsonText) {
    EditorConfig Config;
    try {
        const Json Root = Json::parse(JsonText);
        if (!Root.is_object()) throw std::runtime_error("the file must hold a JSON object");
        for (const auto& Field : Root.items()) {
            const std::string& Key = Field.key();
            const Json& Value = Field.value();
            const auto Known = std::ranges::find(ConfigFields, Key, &ConfigField::Key);
            if (Known == ConfigFields.end()) throw std::runtime_error(std::format("unknown field '{}'", Key));
            if (!Value.is_number()) throw std::runtime_error(std::format("field '{}': must be a number", Key));
            const auto Number = Value.get<float>();
            if (!(Number >= Known->Min && Number <= Known->Max)) {
                throw std::runtime_error(
                    std::format("field '{}': {} is out of [{}, {}]", Key, Number, Known->Min, Known->Max));
            }
            Config.*(Known->Member) = Number;
        }
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Config;
}

EditorConfig loadEditorConfig(const std::filesystem::path& Path) {
    try {
        return parseEditorConfig(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

EditorConfig loadEditorConfigOrDefault(const std::filesystem::path& DataDir) {
    const std::filesystem::path Path = DataDir / "editor.json";
    if (!std::filesystem::exists(Path)) return {};
    return loadEditorConfig(Path);
}

} // namespace fighter::editor
