//===- render/sprites.hpp - Pictures of one fighter -------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares FighterLook, what a fighter looks like (skin, name,
/// equipment), and FighterSprites, the pictures found for it.
///
/// The look comes from the application, not from the fight: combat does not
/// know about pictures. resolveFighterSprites() looks the pictures up once,
/// when a fight starts or visuals are reloaded (F5), and not every frame. A
/// missing picture is not an error: the part is drawn as a capsule and a
/// warning is logged.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <SFML/Graphics/Texture.hpp>

#include "combat/config.hpp"
#include "core/body.hpp"
#include "core/vec2.hpp"
#include "render/visuals.hpp"

namespace fighter::render {

/// One piece of equipment worn by a fighter.
struct ItemLook {
    std::string Id;                  ///< As in data/items/*.json.
    std::vector<BodyPart> Covers;    ///< The parts that get an overlay.
    /// Held in a hand: one picture serves either arm. Missing the picture of
    /// an arm part, the overlay takes the other arm's (a sword drawn for the
    /// right forearm, held in the left hand), with its origin.
    bool Held = false;
};

struct FighterLook {
    std::string Name;                ///< Shown in the HUD.
    std::string Skin;                ///< A key of Visuals::Skins; empty: Visuals::DefaultSkin.
    std::vector<ItemLook> Items;     ///< Overlays are drawn in this order.
};

/// The look of a fighter of a battle: the name and the equipment from its
/// config, the skin chosen by the application. An empty name becomes
/// \p FallbackName.
FighterLook makeFighterLook(const combat::FighterConfig& Config, std::string Skin, std::string_view FallbackName);

/// A picture and how it maps onto a body part.
struct SpriteRef {
    const sf::Texture* Texture = nullptr;
    float MetersPerPixel = 1.0f / 64.0f;
    Vec2 Origin{0.5f, 0.5f};         ///< Fractions of the picture size, see SpritePrim.
};

struct FighterSprites {
    /// A null texture: the part has no picture and is drawn as a capsule.
    PerBodyPart<SpriteRef> Parts{};
    /// Equipment drawn right over each part.
    PerBodyPart<std::vector<SpriteRef>> Overlays{};
    size_t MissingFiles = 0;         ///< Pictures that were expected but not found.
};

/// Returns the texture at a path relative to the project root, or nullptr if
/// it cannot be loaded. \p Smooth selects linear filtering.
using TextureLoader = std::function<const sf::Texture*(const std::string& RelativePath, bool Smooth)>;

/// The path of a picture: `<Dir>/<Part>.png`.
std::string getPicturePath(std::string_view Dir, BodyPart Part);

/// Looks up the pictures of \p Look: `<skin dir>/<Part>.png` for each body
/// part and `<item dir>/<Part>.png` for each part an item covers. Missing
/// files and an unknown skin are logged as warnings, never thrown.
FighterSprites resolveFighterSprites(const Visuals& Vis, const FighterLook& Look, const TextureLoader& Load);

} // namespace fighter::render
