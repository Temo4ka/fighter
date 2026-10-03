#include "render/visuals.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/text_file.hpp"

namespace fighter::render {
namespace {

using Json = nlohmann::json;

const Json& requireObject(const Json& Value, const std::string& Path);
void checkFieldNames(const Json& Object, const std::string& Path, std::initializer_list<std::string_view> Allowed);
std::string joinPath(const std::string& Path, std::string_view Key);
float readFloat(const Json& Object, const std::string& Path, std::string_view Key, float Min, float Max);
std::string readString(const Json& Object, const std::string& Path, std::string_view Key);
combat::ReactionLevel readReaction(const Json& Object, const std::string& Path, std::string_view Key);
SkinDef parseSkin(const Json& Value, const std::string& Path, float DefaultPixelsPerMeter);
ItemVisualDef parseItem(const Json& Value, const std::string& Path);
EffectsParams parseEffects(const Json& Value, const std::string& Path);
HudParams parseHud(const Json& Value, const std::string& Path);

// Bounds that only catch typos (a density of 0, a 30-second flash); the
// values themselves are tuned in sessions (docs/TUNING.md).
constexpr float MaxPixelsPerMeter = 1024.0f;
constexpr float MaxEffectSec = 5.0f;

} // namespace

Visuals parseVisuals(std::string_view JsonText) {
    Visuals Result;
    try {
        const Json Root = Json::parse(JsonText);
        requireObject(Root, "");
        checkFieldNames(Root, "",
                        {"pixels_per_meter", "default_skin", "background", "skins", "items", "effects", "hud"});

        if (Root.contains("pixels_per_meter"))
            Result.PixelsPerMeter = readFloat(Root, "", "pixels_per_meter", 1.0f, MaxPixelsPerMeter);
        if (Root.contains("background")) Result.Background = readString(Root, "", "background");

        if (Root.contains("skins")) {
            for (const auto& [Id, Value] : requireObject(Root.at("skins"), "skins").items())
                Result.Skins.emplace(Id, parseSkin(Value, joinPath("skins", Id), Result.PixelsPerMeter));
        }
        if (Root.contains("items")) {
            for (const auto& [Id, Value] : requireObject(Root.at("items"), "items").items())
                Result.Items.emplace(Id, parseItem(Value, joinPath("items", Id)));
        }
        if (Root.contains("default_skin")) {
            Result.DefaultSkin = readString(Root, "", "default_skin");
            if (!Result.Skins.contains(Result.DefaultSkin)) {
                throw std::runtime_error(
                    std::format("field 'default_skin': '{}' is not one of 'skins'", Result.DefaultSkin));
            }
        }
        if (Root.contains("effects")) Result.Effects = parseEffects(Root.at("effects"), "effects");
        if (Root.contains("hud")) Result.Hud = parseHud(Root.at("hud"), "hud");
    } catch (const Json::exception& Error) {
        throw std::runtime_error(Error.what());
    }
    return Result;
}

Visuals loadVisuals(const std::filesystem::path& Path) {
    try {
        return parseVisuals(readTextFile(Path));
    } catch (const std::runtime_error& Error) {
        throw std::runtime_error(std::format("{}: {}", Path.string(), Error.what()));
    }
}

namespace {

const Json& requireObject(const Json& Value, const std::string& Path) {
    if (!Value.is_object()) {
        throw std::runtime_error(Path.empty() ? std::string("the file must hold a JSON object")
                                              : std::format("field '{}': must be an object", Path));
    }
    return Value;
}

void checkFieldNames(const Json& Object, const std::string& Path, std::initializer_list<std::string_view> Allowed) {
    for (const auto& Field : Object.items()) {
        if (std::ranges::find(Allowed, Field.key()) == Allowed.end())
            throw std::runtime_error(std::format("unknown field '{}'", joinPath(Path, Field.key())));
    }
}

std::string joinPath(const std::string& Path, std::string_view Key) {
    return Path.empty() ? std::string(Key) : std::format("{}.{}", Path, Key);
}

float readFloat(const Json& Object, const std::string& Path, std::string_view Key, float Min, float Max) {
    const std::string Field = joinPath(Path, Key);
    const Json& Value = Object.at(Key);
    if (!Value.is_number()) throw std::runtime_error(std::format("field '{}': {} is not a number", Field, Value.dump()));
    const auto Number = Value.get<float>();
    if (Number < Min || Number > Max)
        throw std::runtime_error(std::format("field '{}': {} is outside [{}, {}]", Field, Number, Min, Max));
    return Number;
}

std::string readString(const Json& Object, const std::string& Path, std::string_view Key) {
    const std::string Field = joinPath(Path, Key);
    const Json& Value = Object.at(Key);
    if (!Value.is_string()) throw std::runtime_error(std::format("field '{}': {} is not a string", Field, Value.dump()));
    auto Text = Value.get<std::string>();
    if (Text.empty()) throw std::runtime_error(std::format("field '{}': must not be empty", Field));
    return Text;
}

combat::ReactionLevel readReaction(const Json& Object, const std::string& Path, std::string_view Key) {
    const std::string Name = readString(Object, Path, Key);
    const auto Level = combat::findReactionLevel(Name);
    if (!Level) {
        throw std::runtime_error(std::format(
            "field '{}': '{}' is not one of None, Touch, Flinch, Stagger, Knockback, Knockdown", joinPath(Path, Key),
            Name));
    }
    return *Level;
}

SkinDef parseSkin(const Json& Value, const std::string& Path, float DefaultPixelsPerMeter) {
    requireObject(Value, Path);
    checkFieldNames(Value, Path, {"dir", "items_dir", "pixels_per_meter", "smooth"});
    SkinDef Skin;
    Skin.Dir = readString(Value, Path, "dir");
    if (Value.contains("items_dir")) Skin.ItemsDir = readString(Value, Path, "items_dir");
    Skin.PixelsPerMeter = Value.contains("pixels_per_meter")
                              ? readFloat(Value, Path, "pixels_per_meter", 1.0f, MaxPixelsPerMeter)
                              : DefaultPixelsPerMeter;
    if (Value.contains("smooth")) {
        const Json& Smooth = Value.at("smooth");
        if (!Smooth.is_boolean())
            throw std::runtime_error(std::format("field '{}': {} is not true or false", joinPath(Path, "smooth"),
                                                 Smooth.dump()));
        Skin.Smooth = Smooth.get<bool>();
    }
    return Skin;
}

ItemVisualDef parseItem(const Json& Value, const std::string& Path) {
    requireObject(Value, Path);
    checkFieldNames(Value, Path, {"dir", "pixels_per_meter", "origins"});
    ItemVisualDef Item;
    if (Value.contains("dir")) Item.Dir = readString(Value, Path, "dir");
    if (Value.contains("pixels_per_meter"))
        Item.PixelsPerMeter = readFloat(Value, Path, "pixels_per_meter", 1.0f, MaxPixelsPerMeter);
    if (Value.contains("origins")) {
        const std::string OriginsPath = joinPath(Path, "origins");
        for (const auto& [PartName, Origin] : requireObject(Value.at("origins"), OriginsPath).items()) {
            const std::string Field = joinPath(OriginsPath, PartName);
            const auto Part = findBodyPart(PartName);
            if (!Part) throw std::runtime_error(std::format("field '{}': '{}' is not a body part", Field, PartName));
            if (!Origin.is_array() || Origin.size() != 2 || !Origin[0].is_number() || !Origin[1].is_number()) {
                throw std::runtime_error(
                    std::format("field '{}': {} is not a pair of numbers [x, y]", Field, Origin.dump()));
            }
            // Slightly outside the picture is allowed: a grip may hang past an edge.
            const Vec2 Point{Origin[0].get<float>(), Origin[1].get<float>()};
            if (Point.X < -1.0f || Point.X > 2.0f || Point.Y < -1.0f || Point.Y > 2.0f) {
                throw std::runtime_error(
                    std::format("field '{}': {} is too far outside the picture (fractions of its size)", Field,
                                Origin.dump()));
            }
            Item.Origins[*Part] = Point;
        }
    }
    return Item;
}

EffectsParams parseEffects(const Json& Value, const std::string& Path) {
    requireObject(Value, Path);
    checkFieldNames(Value, Path, {"hit_flash", "camera_shake", "dust"});
    EffectsParams Effects;

    if (Value.contains("hit_flash")) {
        const std::string Sub = joinPath(Path, "hit_flash");
        const Json& Flash = requireObject(Value.at("hit_flash"), Sub);
        checkFieldNames(Flash, Sub, {"min_reaction", "duration_sec", "radius_m"});
        auto& Out = Effects.HitFlash;
        if (Flash.contains("min_reaction")) Out.MinReaction = readReaction(Flash, Sub, "min_reaction");
        if (Flash.contains("duration_sec")) Out.DurationSec = readFloat(Flash, Sub, "duration_sec", 0.0f, MaxEffectSec);
        if (Flash.contains("radius_m")) Out.RadiusM = readFloat(Flash, Sub, "radius_m", 0.0f, 2.0f);
    }
    if (Value.contains("camera_shake")) {
        const std::string Sub = joinPath(Path, "camera_shake");
        const Json& Shake = requireObject(Value.at("camera_shake"), Sub);
        checkFieldNames(Shake, Sub, {"min_reaction", "amplitude_m", "duration_sec", "frequency_hz"});
        auto& Out = Effects.CameraShake;
        if (Shake.contains("min_reaction")) Out.MinReaction = readReaction(Shake, Sub, "min_reaction");
        if (Shake.contains("amplitude_m")) Out.AmplitudeM = readFloat(Shake, Sub, "amplitude_m", 0.0f, 1.0f);
        if (Shake.contains("duration_sec")) Out.DurationSec = readFloat(Shake, Sub, "duration_sec", 0.0f, MaxEffectSec);
        if (Shake.contains("frequency_hz")) Out.FrequencyHz = readFloat(Shake, Sub, "frequency_hz", 0.0f, 120.0f);
    }
    if (Value.contains("dust")) {
        const std::string Sub = joinPath(Path, "dust");
        const Json& Dust = requireObject(Value.at("dust"), Sub);
        checkFieldNames(Dust, Sub, {"on_knockdown", "particles", "duration_sec", "spread_m", "size_m"});
        auto& Out = Effects.Dust;
        if (Dust.contains("on_knockdown")) {
            const Json& Flag = Dust.at("on_knockdown");
            if (!Flag.is_boolean())
                throw std::runtime_error(std::format("field '{}': {} is not true or false",
                                                     joinPath(Sub, "on_knockdown"), Flag.dump()));
            Out.OnKnockdown = Flag.get<bool>();
        }
        if (Dust.contains("particles")) {
            const float Count = readFloat(Dust, Sub, "particles", 0.0f, 64.0f);
            if (Count != static_cast<float>(static_cast<int>(Count)))
                throw std::runtime_error(std::format("field '{}': {} is not a whole number",
                                                     joinPath(Sub, "particles"), Count));
            Out.Particles = static_cast<int>(Count);
        }
        if (Dust.contains("duration_sec")) Out.DurationSec = readFloat(Dust, Sub, "duration_sec", 0.0f, MaxEffectSec);
        if (Dust.contains("spread_m")) Out.SpreadM = readFloat(Dust, Sub, "spread_m", 0.0f, 3.0f);
        if (Dust.contains("size_m")) Out.SizeM = readFloat(Dust, Sub, "size_m", 0.0f, 0.5f);
    }
    return Effects;
}

HudParams parseHud(const Json& Value, const std::string& Path) {
    requireObject(Value, Path);
    checkFieldNames(Value, Path,
                    {"bar_width_px", "hp_bar_height_px", "stamina_bar_height_px", "margin_px", "gap_px",
                     "name_font_px", "timer_font_px"});
    HudParams Hud;
    if (Value.contains("bar_width_px")) Hud.BarWidthPx = readFloat(Value, Path, "bar_width_px", 10.0f, 2000.0f);
    if (Value.contains("hp_bar_height_px"))
        Hud.HpBarHeightPx = readFloat(Value, Path, "hp_bar_height_px", 1.0f, 200.0f);
    if (Value.contains("stamina_bar_height_px"))
        Hud.StaminaBarHeightPx = readFloat(Value, Path, "stamina_bar_height_px", 0.0f, 200.0f);
    if (Value.contains("margin_px")) Hud.MarginPx = readFloat(Value, Path, "margin_px", 0.0f, 500.0f);
    if (Value.contains("gap_px")) Hud.GapPx = readFloat(Value, Path, "gap_px", 0.0f, 100.0f);
    if (Value.contains("name_font_px"))
        Hud.NameFontPx = static_cast<unsigned>(readFloat(Value, Path, "name_font_px", 6.0f, 128.0f));
    if (Value.contains("timer_font_px"))
        Hud.TimerFontPx = static_cast<unsigned>(readFloat(Value, Path, "timer_font_px", 6.0f, 256.0f));
    return Hud;
}

} // namespace

} // namespace fighter::render
