//===- render/visuals.hpp - Pictures and effects from JSON ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares Visuals, the contents of data/visuals.json: which
/// pictures the renderer puts on body parts and equipment, the parameters of
/// the hit effects and the sizes of the HUD (docs/DATA_FORMATS.md,
/// "visuals.json"; the picture rules are in docs/ART.md).
///
/// The link "object -> picture" lives in this file rather than in code, so a
/// new skin or item overlay is a JSON edit. The file is re-read on F5.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "combat/events.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"

namespace fighter::render {

/// A set of body part pictures: `<Dir>/<Part>.png`, one per body part.
struct SkinDef {
    std::string Dir;
    /// Item overlays drawn in the same style: `<ItemsDir>/<item id>/<Part>.png`.
    /// Empty: only items with their own `dir` get pictures.
    std::string ItemsDir;
    float PixelsPerMeter = 64.0f;
    /// Linear filtering; false keeps pixel art sharp.
    bool Smooth = true;
};

/// Overrides for one item's overlays. Items without an entry use the skin's
/// ItemsDir.
struct ItemVisualDef {
    std::string Dir;                         ///< Empty: `<skin ItemsDir>/<item id>`.
    std::optional<float> PixelsPerMeter;     ///< nullopt: the skin's density.
    /// The point of the picture placed at the part center, as fractions of the
    /// picture size from its top-left corner. Parts not listed use the center
    /// (0.5, 0.5). Needed for a weapon, whose blade sticks out on one side.
    std::map<BodyPart, Vec2> Origins;
};

/// A short flash at the contact point of a strike (O.7, p. 5).
struct HitFlashParams {
    combat::ReactionLevel MinReaction = combat::ReactionLevel::Touch;
    float DurationSec = 0.06f;
    float RadiusM = 0.12f;
};

/// A light camera shake on strong hits.
struct CameraShakeParams {
    combat::ReactionLevel MinReaction = combat::ReactionLevel::Knockback;
    float AmplitudeM = 0.04f;
    float DurationSec = 0.15f;
    float FrequencyHz = 25.0f;
};

/// Dust puffs on the floor when a fighter falls.
struct DustParams {
    bool OnKnockdown = true;
    int Particles = 8;
    float DurationSec = 0.5f;
    float SpreadM = 0.5f;       ///< How far the farthest puff travels from the fall point.
    float SizeM = 0.06f;        ///< Radius of a puff at its largest.
};

struct EffectsParams {
    HitFlashParams HitFlash;
    CameraShakeParams CameraShake;
    DustParams Dust;
};

/// HUD sizes, in window pixels.
struct HudParams {
    float BarWidthPx = 360.0f;
    float HpBarHeightPx = 18.0f;
    float StaminaBarHeightPx = 6.0f;
    float MarginPx = 24.0f;      ///< From the window edges to the bars.
    float GapPx = 4.0f;          ///< Between the bars and the name.
    unsigned NameFontPx = 16;
    unsigned TimerFontPx = 28;
};

/// The pixel render mode (T.4): the world is drawn into a low-resolution
/// picture and scaled up a whole number of times without smoothing, so pixel
/// art keeps square pixels of one size even on rotated body parts.
struct PixelArtParams {
    /// Low-resolution pixels per meter of world; nullopt: the density of the
    /// style's skin, so its pictures land one texel per pixel.
    std::optional<float> PixelsPerMeter;
    /// How many window pixels one low-resolution pixel takes; 0: the largest
    /// whole number at which the camera's view height fits the window.
    int Scale = 0;
    /// true: the view height in meters is kept and the rest of the window
    /// stays black; false: the picture fills the window and the view grows.
    bool Letterbox = true;
};

/// A look of the whole fight: which pictures and how they are drawn. Styles
/// are compared on the same fighter (T.5), switched in the file or by a key.
struct StyleDef {
    std::string Skin;                         ///< Empty: Visuals::DefaultSkin.
    std::optional<PixelArtParams> PixelArt;   ///< nullopt: drawn at window resolution.
};

struct Visuals {
    float PixelsPerMeter = 64.0f;
    /// The active style, a key of Styles; empty when there are no styles.
    std::string Style;
    std::map<std::string, StyleDef, std::less<>> Styles;
    /// The skin of a fighter whose look does not name one.
    std::string DefaultSkin;
    /// The arena background; empty or missing: a plain color.
    std::string Background;
    std::map<std::string, SkinDef, std::less<>> Skins;
    std::map<std::string, ItemVisualDef, std::less<>> Items;
    EffectsParams Effects;
    HudParams Hud;
};

/// Parses visuals.json text. Every key is checked: an unknown key or a bad
/// value throws std::runtime_error naming the field and the value.
Visuals parseVisuals(std::string_view JsonText);

/// The style \p Name of \p Vis; an unknown or empty name gives the plain
/// style (default skin, window resolution).
const StyleDef& getStyle(const Visuals& Vis, std::string_view Name);

/// The skin a fighter of \p Name style wears when its look names none.
std::string_view getStyleSkin(const Visuals& Vis, std::string_view Name);

/// Reads and parses a visuals file; errors name the file.
Visuals loadVisuals(const std::filesystem::path& Path);

} // namespace fighter::render
