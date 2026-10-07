#include "ui/ui_config.hpp"

#include <cmath>
#include <format>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace fighter::ui {
namespace {

using JsonValue = nlohmann::json;

struct ColorField {
    std::string_view Key;
    UiColor UiPalette::* Member;
};

struct SizeField {
    std::string_view Key;
    float UiTypeScale::* Member;
};

constexpr ColorField ColorFields[] = {
    {"background_top", &UiPalette::BackgroundTop}, {"background_bottom", &UiPalette::BackgroundBottom},
    {"floor", &UiPalette::Floor},                  {"panel", &UiPalette::Panel},
    {"track", &UiPalette::Track},                  {"accent", &UiPalette::Accent},
    {"on_accent", &UiPalette::OnAccent},           {"text", &UiPalette::Text},
    {"dim", &UiPalette::Dim},                      {"player1", &UiPalette::Player1},
    {"player2", &UiPalette::Player2},
};

constexpr SizeField SizeFields[] = {
    {"title", &UiTypeScale::Title}, {"heading", &UiTypeScale::Heading}, {"item", &UiTypeScale::Item},
    {"body", &UiTypeScale::Body},   {"hint", &UiTypeScale::Hint},
};

[[noreturn]] void fail(std::string_view Source, const std::string& Message);
double readNumber(std::string_view Source, std::string_view Key, const JsonValue& Value, double Min, double Max);
void readColors(std::string_view Source, const JsonValue& Object, UiPalette& Colors);
void readTypeScale(std::string_view Source, const JsonValue& Object, UiTypeScale& Type);
const JsonValue& requireObject(std::string_view Source, std::string_view Key, const JsonValue& Value);

} // namespace

UiConfig parseUiConfig(std::string_view Text, std::string_view SourceName) {
    JsonValue Root;
    try {
        Root = JsonValue::parse(Text);
    } catch (const JsonValue::exception& Error) {
        fail(SourceName, Error.what());
    }
    requireObject(SourceName, "(root)", Root);

    UiConfig Config;
    for (const auto& [Key, Value] : Root.items()) {
        if (Key == "results_delay_sec") {
            Config.ResultsDelaySec = readNumber(SourceName, Key, Value, 0.0, 60.0);
        } else if (Key == "highlight_sec") {
            Config.HighlightSec = readNumber(SourceName, Key, Value, 0.0, 5.0);
        } else if (Key == "dim_alpha") {
            Config.DimAlpha = static_cast<int>(readNumber(SourceName, Key, Value, 0.0, 255.0));
        } else if (Key == "colors") {
            readColors(SourceName, requireObject(SourceName, Key, Value), Config.Colors);
        } else if (Key == "type_scale") {
            readTypeScale(SourceName, requireObject(SourceName, Key, Value), Config.Type);
        } else {
            fail(SourceName, std::format("unknown key \"{}\"", Key));
        }
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

bool parseUiColor(std::string_view Text, UiColor& Out) {
    if ((Text.size() != 7 && Text.size() != 9) || Text[0] != '#') return false;
    uint8_t Bytes[4] = {0, 0, 0, 255};
    for (size_t Index = 0; Index < (Text.size() - 1) / 2; ++Index) {
        unsigned Value = 0;
        for (const char Digit : Text.substr(1 + Index * 2, 2)) {
            Value <<= 4;
            if (Digit >= '0' && Digit <= '9') {
                Value |= static_cast<unsigned>(Digit - '0');
            } else if (Digit >= 'a' && Digit <= 'f') {
                Value |= static_cast<unsigned>(Digit - 'a' + 10);
            } else if (Digit >= 'A' && Digit <= 'F') {
                Value |= static_cast<unsigned>(Digit - 'A' + 10);
            } else {
                return false;
            }
        }
        Bytes[Index] = static_cast<uint8_t>(Value);
    }
    Out = {Bytes[0], Bytes[1], Bytes[2], Bytes[3]};
    return true;
}

float easeToward(float Position, float Target, double Dt, double TimeSec) {
    if (TimeSec <= 0.0 || Dt <= 0.0) return TimeSec <= 0.0 ? Target : Position;
    const auto Blend = static_cast<float>(1.0 - std::exp(-Dt / TimeSec));
    return Position + (Target - Position) * Blend;
}

namespace {

void fail(std::string_view Source, const std::string& Message) {
    throw std::runtime_error(std::format("{}: {}", Source, Message));
}

double readNumber(std::string_view Source, std::string_view Key, const JsonValue& Value, double Min, double Max) {
    if (!Value.is_number() || !std::isfinite(Value.get<double>()) || Value.get<double>() < Min ||
        Value.get<double>() > Max) {
        fail(Source, std::format("{} must be a number in [{}, {}], got {}", Key, Min, Max, Value.dump()));
    }
    return Value.get<double>();
}

const JsonValue& requireObject(std::string_view Source, std::string_view Key, const JsonValue& Value) {
    if (!Value.is_object()) fail(Source, std::format("{} must be an object, got {}", Key, Value.dump()));
    return Value;
}

void readColors(std::string_view Source, const JsonValue& Object, UiPalette& Colors) {
    for (const auto& [Key, Value] : Object.items()) {
        const ColorField* Field = nullptr;
        for (const ColorField& Candidate : ColorFields) {
            if (Candidate.Key == Key) Field = &Candidate;
        }
        if (!Field) fail(Source, std::format("unknown key \"colors.{}\"", Key));
        UiColor Parsed;
        if (!Value.is_string() || !parseUiColor(Value.get<std::string>(), Parsed))
            fail(Source, std::format("colors.{} must be \"#rrggbb\" or \"#rrggbbaa\", got {}", Key, Value.dump()));
        Colors.*(Field->Member) = Parsed;
    }
}

void readTypeScale(std::string_view Source, const JsonValue& Object, UiTypeScale& Type) {
    for (const auto& [Key, Value] : Object.items()) {
        const SizeField* Field = nullptr;
        for (const SizeField& Candidate : SizeFields) {
            if (Candidate.Key == Key) Field = &Candidate;
        }
        if (!Field) fail(Source, std::format("unknown key \"type_scale.{}\"", Key));
        Type.*(Field->Member) = static_cast<float>(readNumber(Source, "type_scale." + Key, Value, 0.005, 0.5));
    }
}

} // namespace

} // namespace fighter::ui
