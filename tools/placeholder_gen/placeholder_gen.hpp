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
/// A picture covers the part's shape plus a joint overlap at both ends of its
/// long axis, and is centred on the shape centre, which is where combat puts
/// PartTransform::Position. The overlap runs along Y for limbs, torso, pelvis
/// and head, and along X for a part lying on the floor (the foot, whose toe
/// points right). Two variants share the geometry: Pixel (hard edges, 1-px
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

/// Share of the shape's length that the picture extends past each end.
inline constexpr float DefaultOverlap = 0.15f;

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
/// pose, density and centre rule as the part itself. A weapon's blade
/// extends past the fist by its reach; the picture is then symmetric about
/// the forearm centre (the upper half stays empty), so no anchor is needed.
sf::Image drawItemOverlay(const stats::EquipmentItem& Item, const PartGeometry& Geometry, Variant Kind,
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
