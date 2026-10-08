//===- placeholder_gen/placeholder_gen.hpp - Placeholder images -*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the generator of placeholder sprites: one picture per
/// body part, drawn by code from the shapes in data/rigs/<rig>.json, and
/// overlays for the items named in data/visuals.json. The rules the pictures
/// follow are in docs/ART.md.
///
/// A picture covers exactly the part's shape, so it matches the debug shapes,
/// and is centred on the shape centre, which is where combat puts
/// PartTransform::Position. An optional joint overlap (none by default)
/// extends it at both ends of its long axis: along Y for limbs, torso, pelvis
/// and head, along X for a part lying on the floor (the foot, whose toe points
/// right). The head always gets room below for a neck stub. Two variants share the geometry: Pixel (hard edges, 1-px
/// outline, flat colours, 32 px/m) and Smooth (anti-aliased, soft shading,
/// 64 px/m).
///
/// \code
///   tools::GenerateOptions Options;
///   Options.DataDir = "data";
///   Options.OutDir = "assets/placeholders";
///   tools::generatePlaceholders(Options);
///   // -> assets/placeholders/pixel/humanoid/Head.png, ...
///   //    assets/placeholders/smooth/items/iron_helmet/Head.png, ...
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <SFML/Graphics/Image.hpp>

#include "core/body.hpp"
#include "core/vec2.hpp"
#include "physics/body.hpp"
#include "rig/rig_def.hpp"
#include "stats/stats.hpp"

namespace fighter::tools {

enum class Variant { Pixel, Smooth };

/// Share of the shape's length that the picture extends past each end. None:
/// capsule caps already cover the joints, and an overlap sticks out at the free
/// ends (fist, toe, top of the torso).
inline constexpr float DefaultOverlap = 0.0f;

/// Room below the head for the neck stub, as a share of the head's diameter.
inline constexpr float NeckShare = 0.15f;

std::string_view getVariantName(Variant Kind);
float getDefaultPixelsPerMeter(Variant Kind);

/// Where and how large the picture of one body part is, in meters. The local
/// frame of the picture is the part's frame: origin at the picture centre (the
/// shape centre), X to the right, Y up, reference pose.
struct PartGeometry {
    BodyPart Part = BodyPart::Torso;
    physics::ShapeKind Kind = physics::ShapeKind::Capsule;
    Vec2 Center;         ///< Shape centre in the rig frame = picture centre.
    Vec2 ShapeSize;      ///< Width and height of the shape, rounding included.
    Vec2 Padding;        ///< Overlap added at each end; one component is zero.
    Vec2 HalfExtents;    ///< Box: half sizes without rounding.
    float Radius = 0.0f; ///< Circle and capsule radius, box rounding.

    /// Size of the picture: the shape plus the overlap at both ends.
    Vec2 getImageSize() const { return {ShapeSize.X + 2.0f * Padding.X, ShapeSize.Y + 2.0f * Padding.Y}; }
};

/// Geometry of the picture of \p Part; \p Overlap is the share of the shape
/// length added at both ends (DefaultOverlap).
PartGeometry computePartGeometry(const rig::PartDef& Part, float Overlap = DefaultOverlap);

/// Number of pixels that cover \p Meters at \p PixelsPerMeter (rounded up).
unsigned getImageSizePixels(float Meters, float PixelsPerMeter);

/// Draws the body part: transparent background, the picture centred on the
/// shape centre. \p PixelsPerMeter defaults to the variant's own density
/// when zero.
sf::Image drawPart(const PartGeometry& Geometry, Variant Kind, float PixelsPerMeter = 0.0f);

/// Draws the overlay of \p Item for one of the parts it covers: the same
/// pose, density and centre rule as the part itself. Throws
/// std::invalid_argument for a weapon, which is a body of its own
/// (drawWeapon()).
sf::Image drawItemOverlay(const stats::EquipmentItem& Item, const PartGeometry& Geometry, Variant Kind,
                          float PixelsPerMeter = 0.0f);

/// The capsule of a held weapon as the rig builds it (rig::Rig, the weapon
/// body): from the center of the fist along the forearm's axis, so that its
/// surface ends the reach beyond the fist's surface. In the picture's frame
/// the capsule is centered (rig::Rig::getWeaponTransforms puts the picture
/// center there) and runs from the fist (up, +Y) to the tip (down, -Y).
struct WeaponGeometry {
    float Segment = 0.0f;       ///< Between the centers of the capsule's caps, m.
    float Radius = 0.0f;        ///< Of the capsule: half the weapon's width, m.
    float HolderRadius = 0.0f;  ///< Of the holding forearm (the fist), m.
    float Reach = 0.0f;         ///< Beyond the fist's surface, m.

    /// The capsule with its caps (PartTransform::Size.Y), m.
    float getLength() const { return Segment + 2.0f * Radius; }
    /// Where the fist's surface is in the picture's frame (Y), m.
    float getFistY() const { return Segment * 0.5f - HolderRadius; }
};

/// The weapon geometry of \p Weapon held by the main hand of \p Rig: its
/// width_m, else the mount's width (RigDef::Weapon).
WeaponGeometry computeWeaponGeometry(const stats::WeaponProps& Weapon, const rig::RigDef& Rig);

/// Draws the picture of a held weapon (Weapon.png of the item): the blade or
/// head along -Y from the hilt in the fist to the tip, as wide as the weapon,
/// centered on its capsule (the hilt behind the fist is inside the picture,
/// which is padded symmetrically). Needs \p Item.Weapon.
sf::Image drawWeapon(const stats::EquipmentItem& Item, const WeaponGeometry& Geometry, Variant Kind,
                     float PixelsPerMeter = 0.0f);

struct GenerateOptions {
    std::filesystem::path DataDir;   ///< Holds rigs/, items/ and visuals.json.
    std::filesystem::path OutDir;    ///< Gets {pixel,smooth}/<rig>/ and {pixel,smooth}/items/<id>/.
    std::string RigName = "humanoid";
    std::vector<Variant> Variants = {Variant::Pixel, Variant::Smooth};
    float Overlap = DefaultOverlap;
    /// If not empty, also writes a contact sheet of the assembled fighter in
    /// the reference pose, with every item, one column per variant.
    std::filesystem::path PreviewPath;
};

/// Writes the pictures of every part of the rig and the overlays of the items
/// listed under "items" in visuals.json. Returns the written files. Throws
/// std::runtime_error naming the problem (unreadable data, unknown item id,
/// file that cannot be written).
std::vector<std::filesystem::path> generatePlaceholders(const GenerateOptions& Options);

} // namespace fighter::tools
