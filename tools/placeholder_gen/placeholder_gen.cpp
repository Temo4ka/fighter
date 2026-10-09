#include "placeholder_gen/placeholder_gen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/log.hpp"
#include "core/text_file.hpp"
#include "stats/loading.hpp"

namespace fighter::tools {
namespace {

using Json = nlohmann::json;

/// Part of the picture that reaches past an armor piece's silhouette, m.
constexpr float ArmorMargin = 0.012f;
/// A part at least this many times wider than tall lies on the floor (foot).
constexpr float LyingAspect = 2.0f;
/// Darkening of the outline colour relative to the fill.
constexpr float OutlineShade = 0.55f;
/// Narrowest picture of a weapon: wide enough for the cross guard, m.
constexpr float MinWeaponWidth = 0.14f;
/// How far the hilt and pommel stick out behind the fist's surface, m.
constexpr float HiltLength = 0.12f;
/// How far the haft of a blunt weapon sticks out behind the fist, m.
constexpr float HaftTail = 0.2f;
/// The haft and the head of a blunt weapon (half across the haft, half along
/// it), as shares of its width.
constexpr float HaftShare = 0.6f;
constexpr float HeadAcrossShare = 4.0f;
constexpr float HeadAlongShare = 2.4f;
constexpr float RadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

struct Rgb {
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
};

/// Signed distance in meters to a shape in the picture's local frame (X right,
/// Y up, origin at the picture centre): negative inside.
using Sdf = std::function<float(Vec2)>;

/// One flat or shaded shape of a picture. Layers are painted in order.
struct Layer {
    Sdf Shape;
    /// Paints only onto what is already drawn and leaves the alpha alone, so a
    /// mark never changes the silhouette.
    bool Atop = false;
    Rgb Fill;
    float HalfThickness = 0.05f; ///< Depth at which soft shading reaches full brightness, m.
    bool Outlined = true;      ///< Pixel variant: 1-px darker outline.
    bool Shaded = true;        ///< Smooth variant: darker towards the edges.
};

enum class Region { Head, Torso, Pelvis, Arm, Leg };

Region getRegion(BodyPart Part);
Rgb fromHsv(float HueDegrees, float Saturation, float Value);
Rgb scaled(Rgb Color, float Factor);
Rgb mixed(Rgb A, Rgb B, float T);
Rgb getPartColor(BodyPart Part);
bool isLying(Vec2 ShapeSize);
float getSdfCircle(Vec2 Point, Vec2 Center, float Radius);
float getSdfBox(Vec2 Point, Vec2 Center, Vec2 Inner, float Rounding);
float getSdfCapsule(Vec2 Point, float HalfSegment, float Radius);
float getSdfTriangle(Vec2 Point, Vec2 A, Vec2 B, Vec2 C);
Sdf makeBodySdf(const PartGeometry& Geometry);
float getHalfThickness(const PartGeometry& Geometry);
void addPartMarks(const PartGeometry& Geometry, Rgb Base, std::vector<Layer>& Layers);
sf::Image renderLayers(Vec2 SizeMeters, float PixelsPerMeter, Variant Kind, const std::vector<Layer>& Layers);
std::vector<Layer> makeHelmetLayers(const PartGeometry& Geometry, Rgb Metal);
std::vector<Layer> makeShellLayers(const PartGeometry& Geometry, Rgb Metal);
/// The layers of a weapon whose fist surface is at \p Fist (Y) and whose
/// blade is \p HalfWidth wide on either side of the axis.
std::vector<Layer> makeWeaponLayers(const stats::WeaponProps& Weapon, std::string_view MoveSet, float Fist,
                                    float HalfWidth);
std::vector<Layer> makeShieldLayers(const stats::ShieldProps& Shield);
Rgb getArmorColor(float Armor);
sf::Image drawReferencePose(const rig::RigDef& Rig, Variant Kind, float Overlap,
                            const std::vector<const stats::EquipmentItem*>& Items);
void blit(sf::Image& Target, const sf::Image& Source, sf::Vector2i Center, unsigned Scale);
std::vector<std::string> readVisualItemIds(const std::filesystem::path& VisualsPath);
void saveImage(const sf::Image& Image, const std::filesystem::path& Path);

} // namespace

std::string_view getVariantName(Variant Kind) { return Kind == Variant::Pixel ? "pixel" : "smooth"; }

float getDefaultPixelsPerMeter(Variant Kind) { return Kind == Variant::Pixel ? 32.0f : 64.0f; }

PartGeometry computePartGeometry(const rig::PartDef& Part, float Overlap) {
    PartGeometry Result;
    Result.Part = Part.Part;
    Result.Kind = Part.Shape;
    Result.Radius = Part.Radius;

    switch (Part.Shape) {
    case physics::ShapeKind::Circle:
        Result.Center = Part.Center;
        Result.ShapeSize = {2.0f * Part.Radius, 2.0f * Part.Radius};
        break;
    case physics::ShapeKind::Capsule: {
        Result.Center = (Part.Begin + Part.End) * 0.5f;
        const float Length = (Part.End - Part.Begin).getLength() + 2.0f * Part.Radius;
        Result.ShapeSize = {2.0f * Part.Radius, Length};
        break;
    }
    case physics::ShapeKind::Box:
        Result.Center = Part.Center;
        Result.HalfExtents = Part.HalfExtents;
        Result.ShapeSize = {2.0f * (Part.HalfExtents.X + Part.Radius), 2.0f * (Part.HalfExtents.Y + Part.Radius)};
        break;
    }

    if (isLying(Result.ShapeSize)) {
        Result.Padding = {Overlap * Result.ShapeSize.X, 0.0f};
    } else {
        Result.Padding = {0.0f, Overlap * Result.ShapeSize.Y};
    }
    if (Part.Shape == physics::ShapeKind::Circle) {
        Result.Padding.Y = std::max(Result.Padding.Y, NeckShare * Result.ShapeSize.Y);
    }
    return Result;
}

unsigned getImageSizePixels(float Meters, float PixelsPerMeter) {
    // The epsilon keeps 0.25 m * 64 px/m from becoming 17 px through float noise.
    const float Pixels = std::ceil(Meters * PixelsPerMeter - 1e-3f);
    return static_cast<unsigned>(std::max(Pixels, 1.0f));
}

sf::Image drawPart(const PartGeometry& Geometry, Variant Kind, float PixelsPerMeter) {
    if (PixelsPerMeter <= 0.0f) PixelsPerMeter = getDefaultPixelsPerMeter(Kind);

    const Rgb Base = getPartColor(Geometry.Part);
    Layer Body;
    Body.Shape = makeBodySdf(Geometry);
    Body.Fill = Base;
    Body.HalfThickness = getHalfThickness(Geometry);

    std::vector<Layer> Layers;
    Layers.push_back(std::move(Body));
    addPartMarks(Geometry, Base, Layers);
    return renderLayers(Geometry.getImageSize(), PixelsPerMeter, Kind, Layers);
}

sf::Image drawItemOverlay(const stats::EquipmentItem& Item, const PartGeometry& Geometry, Variant Kind,
                          float PixelsPerMeter) {
    if (PixelsPerMeter <= 0.0f) PixelsPerMeter = getDefaultPixelsPerMeter(Kind);

    Vec2 Size = Geometry.getImageSize() + Vec2{2.0f * ArmorMargin, 2.0f * ArmorMargin};
    std::vector<Layer> Layers;
    if (Item.Weapon) {
        throw std::invalid_argument(std::format("item '{}': a weapon has no overlay, it is drawn by drawWeapon()", Item.Id));
    }
    if (Item.Shield) {
        Layers = makeShieldLayers(*Item.Shield);
        // The plate is centered on the forearm, as the body's (rig.cpp).
        Size = {std::max(Size.X, Item.Shield->WidthM + 2.0f * ArmorMargin),
                std::max(Size.Y, Item.Shield->LengthM + 2.0f * ArmorMargin)};
    } else if (Item.Slot == stats::EquipmentSlot::Head) {
        Layers = makeHelmetLayers(Geometry, getArmorColor(Item.Armor));
    } else {
        Layers = makeShellLayers(Geometry, getArmorColor(Item.Armor));
    }
    return renderLayers(Size, PixelsPerMeter, Kind, Layers);
}

WeaponGeometry computeWeaponGeometry(const stats::WeaponProps& Weapon, const rig::RigDef& Rig) {
    WeaponGeometry Result;
    Result.Radius = Weapon.WidthM.value_or(Rig.Weapon.Width) * 0.5f;
    Result.HolderRadius = Rig.getPart(Rig.Weapon.Part).Radius;
    Result.Reach = Weapon.ReachM;
    Result.Segment = std::max(Result.HolderRadius + Weapon.ReachM - Result.Radius, 0.0f);
    return Result;
}

sf::Image drawWeapon(const stats::EquipmentItem& Item, const WeaponGeometry& Geometry, Variant Kind,
                     float PixelsPerMeter) {
    if (!Item.Weapon) throw std::invalid_argument(std::format("item '{}' is not a weapon", Item.Id));
    if (PixelsPerMeter <= 0.0f) PixelsPerMeter = getDefaultPixelsPerMeter(Kind);
    const float Fist = Geometry.getFistY();
    const std::vector<Layer> Layers = makeWeaponLayers(*Item.Weapon, Item.MoveSet, Fist, Geometry.Radius);
    // Symmetric about the capsule's center so that the centre rule holds:
    // the hilt behind the fist or the tip, whichever is further.
    const float Behind = Item.MoveSet.contains("sword") ? HiltLength : HaftTail;
    const float HalfHeight = std::max(Geometry.getLength() * 0.5f, Fist + Behind) + ArmorMargin;
    const float Width = std::max(2.0f * Geometry.Radius * HeadAcrossShare, MinWeaponWidth);
    return renderLayers({Width, 2.0f * HalfHeight}, PixelsPerMeter, Kind, Layers);
}

std::vector<std::filesystem::path> generatePlaceholders(const GenerateOptions& Options) {
    const auto RigPath = Options.DataDir / "rigs" / (Options.RigName + ".json");
    const rig::RigDef Rig = rig::loadRigDef(RigPath);

    std::vector<std::string> ItemIds = readVisualItemIds(Options.DataDir / "visuals.json");
    std::optional<stats::ItemCatalog> Catalog;
    if (!ItemIds.empty()) Catalog = stats::loadItemCatalog(Options.DataDir / "items");

    std::vector<const stats::EquipmentItem*> Items;
    for (const auto& Id : ItemIds) {
        const stats::EquipmentItem* Item = Catalog->findItem(Id);
        if (!Item) {
            throw std::runtime_error(std::format("{}: item '{}' from \"items\" is not in {}", "visuals.json", Id,
                                                 (Options.DataDir / "items").string()));
        }
        Items.push_back(Item);
    }

    std::vector<std::filesystem::path> Written;
    for (const Variant Kind : Options.Variants) {
        const auto VariantDir = Options.OutDir / getVariantName(Kind);

        for (const auto& Part : Rig.Parts) {
            const auto Geometry = computePartGeometry(Part, Options.Overlap);
            const auto Path = VariantDir / Options.RigName / (std::string(getBodyPartName(Part.Part)) + ".png");
            saveImage(drawPart(Geometry, Kind), Path);
            Written.push_back(Path);
        }

        for (const stats::EquipmentItem* Item : Items) {
            // A weapon is a body of its own: one picture for either hand.
            if (Item->Weapon) {
                const auto Path = VariantDir / "items" / Item->Id / "Weapon.png";
                saveImage(drawWeapon(*Item, computeWeaponGeometry(*Item->Weapon, Rig), Kind), Path);
                Written.push_back(Path);
                continue;
            }
            // An item held in a hand covers the holding forearm, which the
            // fighter sheet chooses: draw both.
            const std::vector<BodyPart> Parts = stats::isHandSlot(Item->Slot)
                                                    ? std::vector<BodyPart>{BodyPart::ForearmR, BodyPart::ForearmL}
                                                    : Item->Covers;
            for (const BodyPart Covered : Parts) {
                const auto Geometry = computePartGeometry(Rig.getPart(Covered), Options.Overlap);
                const auto Path = VariantDir / "items" / Item->Id / (std::string(getBodyPartName(Covered)) + ".png");
                saveImage(drawItemOverlay(*Item, Geometry, Kind), Path);
                Written.push_back(Path);
            }
        }
    }

    if (!Options.PreviewPath.empty()) {
        // Pixel and smooth side by side, both at 256 px/m.
        std::vector<sf::Image> Sheets;
        for (const Variant Kind : Options.Variants) {
            Sheets.push_back(drawReferencePose(Rig, Kind, Options.Overlap, Items));
        }
        const sf::Vector2u Cell = Sheets.empty() ? sf::Vector2u{1, 1} : Sheets.front().getSize();
        sf::Image Sheet(sf::Vector2u{Cell.x * static_cast<unsigned>(std::max<size_t>(Sheets.size(), 1)), Cell.y},
                        sf::Color::Transparent);
        for (unsigned Index = 0; Index < Sheets.size(); ++Index) {
            if (!Sheet.copy(Sheets[Index], {Index * Cell.x, 0})) throw std::runtime_error("cannot build the preview");
        }
        saveImage(Sheet, Options.PreviewPath);
        Written.push_back(Options.PreviewPath);
    }
    return Written;
}

namespace {

Region getRegion(BodyPart Part) {
    switch (Part) {
    case BodyPart::Head: return Region::Head;
    case BodyPart::Torso: return Region::Torso;
    case BodyPart::Pelvis: return Region::Pelvis;
    case BodyPart::UpperArmL:
    case BodyPart::ForearmL:
    case BodyPart::UpperArmR:
    case BodyPart::ForearmR: return Region::Arm;
    default: return Region::Leg;
    }
}

/// Hue in degrees, saturation and value in 0..1.
Rgb fromHsv(float HueDegrees, float Saturation, float Value) {
    const float Hue = std::fmod(HueDegrees, 360.0f) / 60.0f;
    const float Chroma = Value * Saturation;
    const float Second = Chroma * (1.0f - std::fabs(std::fmod(Hue, 2.0f) - 1.0f));
    const float Floor = Value - Chroma;
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    if (Hue < 1.0f) { R = Chroma; G = Second; }
    else if (Hue < 2.0f) { R = Second; G = Chroma; }
    else if (Hue < 3.0f) { G = Chroma; B = Second; }
    else if (Hue < 4.0f) { G = Second; B = Chroma; }
    else if (Hue < 5.0f) { R = Second; B = Chroma; }
    else { R = Chroma; B = Second; }
    return {R + Floor, G + Floor, B + Floor};
}

Rgb scaled(Rgb Color, float Factor) {
    return {std::clamp(Color.R * Factor, 0.0f, 1.0f), std::clamp(Color.G * Factor, 0.0f, 1.0f),
            std::clamp(Color.B * Factor, 0.0f, 1.0f)};
}

Rgb mixed(Rgb A, Rgb B, float T) {
    return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
}

/// A distinct hue per region; parts of the near side (L) are lighter than the
/// far side (R) so that poses read well.
Rgb getPartColor(BodyPart Part) {
    const std::string_view Name = getBodyPartName(Part);
    const bool IsNear = Name.ends_with('L');
    const bool IsFar = Name.ends_with('R');
    const float Value = IsNear ? 0.95f : (IsFar ? 0.58f : 0.78f);
    switch (getRegion(Part)) {
    case Region::Head: return fromHsv(28.0f, 0.38f, 0.92f);
    case Region::Torso: return fromHsv(212.0f, 0.60f, Value);
    case Region::Pelvis: return fromHsv(278.0f, 0.55f, Value);
    case Region::Arm: return fromHsv(128.0f, 0.60f, Value);
    case Region::Leg: return fromHsv(4.0f, 0.68f, Value);
    }
    return {};
}

bool isLying(Vec2 ShapeSize) { return ShapeSize.X >= LyingAspect * ShapeSize.Y; }

float getSdfCircle(Vec2 Point, Vec2 Center, float Radius) { return (Point - Center).getLength() - Radius; }

/// A rounded box: \p Inner is the half size without the rounding, so the
/// outline is \p Rounding larger on every side.
float getSdfBox(Vec2 Point, Vec2 Center, Vec2 Inner, float Rounding) {
    const float QX = std::fabs(Point.X - Center.X) - Inner.X;
    const float QY = std::fabs(Point.Y - Center.Y) - Inner.Y;
    const float Outside = Vec2{std::max(QX, 0.0f), std::max(QY, 0.0f)}.getLength();
    return Outside + std::min(std::max(QX, QY), 0.0f) - Rounding;
}

/// A vertical capsule through the origin.
float getSdfCapsule(Vec2 Point, float HalfSegment, float Radius) {
    const float Clamped = std::clamp(Point.Y, -HalfSegment, HalfSegment);
    return Vec2{Point.X, Point.Y - Clamped}.getLength() - Radius;
}

float getSdfTriangle(Vec2 Point, Vec2 A, Vec2 B, Vec2 C) {
    const Vec2 Edges[3] = {B - A, C - B, A - C};
    const Vec2 Corners[3] = {A, B, C};
    const float Winding = Edges[0].X * Edges[2].Y - Edges[0].Y * Edges[2].X < 0.0f ? -1.0f : 1.0f;
    float MinDistSq = 1e30f;
    float MinSide = 1e30f;
    for (size_t Index = 0; Index < 3; ++Index) {
        const Vec2 ToPoint = Point - Corners[Index];
        const Vec2 Edge = Edges[Index];
        const float Along = std::clamp(dot(ToPoint, Edge) / dot(Edge, Edge), 0.0f, 1.0f);
        const Vec2 Closest = ToPoint - Edge * Along;
        MinDistSq = std::min(MinDistSq, dot(Closest, Closest));
        MinSide = std::min(MinSide, Winding * (ToPoint.X * Edge.Y - ToPoint.Y * Edge.X));
    }
    return -std::sqrt(MinDistSq) * (MinSide < 0.0f ? -1.0f : 1.0f);
}

/// The silhouette of the part, overlap included. The capsule and the box are
/// stretched by the padding; the head gets a neck stub in its bottom padding,
/// because a bare circle would leave a gap to the torso when it turns.
Sdf makeBodySdf(const PartGeometry& Geometry) {
    switch (Geometry.Kind) {
    case physics::ShapeKind::Capsule: {
        const float HalfSegment = std::max(Geometry.getImageSize().Y * 0.5f - Geometry.Radius, 0.0f);
        const float Radius = Geometry.Radius;
        return [HalfSegment, Radius](Vec2 Point) { return getSdfCapsule(Point, HalfSegment, Radius); };
    }
    case physics::ShapeKind::Box: {
        const Vec2 Inner = Geometry.HalfExtents + Geometry.Padding;
        const float Rounding = Geometry.Radius;
        return [Inner, Rounding](Vec2 Point) { return getSdfBox(Point, {}, Inner, Rounding); };
    }
    case physics::ShapeKind::Circle: {
        const float Radius = Geometry.Radius;
        const float NeckTop = -0.4f * Radius;
        const float NeckBottom = -(Radius + Geometry.Padding.Y);
        const Vec2 NeckCenter{0.0f, (NeckTop + NeckBottom) * 0.5f};
        const Vec2 NeckHalf{0.45f * Radius, (NeckTop - NeckBottom) * 0.5f};
        return [Radius, NeckCenter, NeckHalf](Vec2 Point) {
            return std::min(getSdfCircle(Point, {}, Radius), getSdfBox(Point, NeckCenter, NeckHalf, 0.0f));
        };
    }
    }
    return [](Vec2) { return 1.0f; };
}

float getHalfThickness(const PartGeometry& Geometry) {
    return std::max(std::min(Geometry.ShapeSize.X, Geometry.ShapeSize.Y) * 0.5f, 0.01f);
}

/// Small marks that show where the part faces (right) and make mirroring
/// visible: eye and nose on the head, a front stripe on limbs and torso, a
/// belt buckle on the pelvis, a glove on the forearm, a toe cap on the foot.
void addPartMarks(const PartGeometry& Geometry, Rgb Base, std::vector<Layer>& Layers) {
    const Vec2 Size = Geometry.getImageSize();
    const float Radius = Geometry.Radius;

    auto addMark = [&](Sdf Shape, Rgb Color) {
        Layer Mark;
        Mark.Shape = std::move(Shape);
        Mark.Atop = true;
        Mark.Fill = Color;
        Mark.Outlined = false;
        Mark.Shaded = false;
        Layers.push_back(std::move(Mark));
    };
    auto addBox = [&](Vec2 Center, Vec2 Half, Rgb Color) {
        addMark([Center, Half](Vec2 Point) { return getSdfBox(Point, Center, Half, 0.0f); }, Color);
    };
    auto addCircle = [&](Vec2 Center, float MarkRadius, Rgb Color) {
        addMark([Center, MarkRadius](Vec2 Point) { return getSdfCircle(Point, Center, MarkRadius); }, Color);
    };

    switch (Geometry.Part) {
    case BodyPart::Head:
        addCircle({0.78f * Radius, -0.08f * Radius}, 0.17f * Radius, scaled(Base, 0.78f));
        addCircle({0.45f * Radius, 0.22f * Radius}, 0.13f * Radius, {0.08f, 0.08f, 0.12f});
        break;
    case BodyPart::Pelvis:
        addBox({0.0f, 0.25f * Size.Y}, {Size.X * 0.5f, 0.11f * Size.Y}, scaled(Base, 0.55f));
        addBox({0.30f * Size.X, 0.25f * Size.Y}, {0.07f * Size.X, 0.14f * Size.Y}, {0.95f, 0.85f, 0.4f});
        break;
    case BodyPart::ForearmL:
    case BodyPart::ForearmR:
        // The glove sits at the lower end: the fist.
        addCircle({0.0f, -(Size.Y * 0.5f - Radius)}, Radius * 1.05f, scaled(Base, 0.62f));
        break;
    case BodyPart::FootL:
    case BodyPart::FootR: {
        const float HalfX = Size.X * 0.5f;
        addBox({HalfX - 0.09f * Size.X, 0.0f}, {0.09f * Size.X, Size.Y}, scaled(Base, 0.5f));
        break;
    }
    default:
        if (Geometry.Kind == physics::ShapeKind::Capsule) {
            const float HalfLength = Size.Y * 0.5f - Radius;
            addBox({0.5f * Radius, 0.0f}, {0.14f * Radius, std::max(HalfLength * 0.8f, 0.2f * Radius)},
                   scaled(Base, 1.25f));
        }
        break;
    }
}

/// Paints the layers into an image of \p SizeMeters at the given density. The
/// image centre is the local origin, so a shape centred there stays centred.
sf::Image renderLayers(Vec2 SizeMeters, float PixelsPerMeter, Variant Kind, const std::vector<Layer>& Layers) {
    const unsigned Width = getImageSizePixels(SizeMeters.X, PixelsPerMeter);
    const unsigned Height = getImageSizePixels(SizeMeters.Y, PixelsPerMeter);
    sf::Image Image(sf::Vector2u{Width, Height}, sf::Color::Transparent);

    const bool Hard = Kind == Variant::Pixel;
    // Distance in meters -> coverage of the pixel: a step for the pixel
    // variant, a one-pixel ramp for the smooth one.
    auto getCoverage = [&](float Distance) {
        if (Hard) return Distance < 0.0f ? 1.0f : 0.0f;
        return std::clamp(0.5f - Distance * PixelsPerMeter, 0.0f, 1.0f);
    };
    auto toByte = [](float Value) { return static_cast<uint8_t>(std::lround(std::clamp(Value, 0.0f, 1.0f) * 255.0f)); };

    for (unsigned Row = 0; Row < Height; ++Row) {
        for (unsigned Column = 0; Column < Width; ++Column) {
            // Pixel centres are exactly symmetric about the image centre, so
            // the silhouette is too.
            const Vec2 Point{(static_cast<float>(Column) + 0.5f - static_cast<float>(Width) * 0.5f) / PixelsPerMeter,
                             -(static_cast<float>(Row) + 0.5f - static_cast<float>(Height) * 0.5f) / PixelsPerMeter};
            Rgb Color;
            float Alpha = 0.0f;
            for (const Layer& Item : Layers) {
                const float Distance = Item.Shape(Point);
                const float Coverage = getCoverage(Distance);
                if (Coverage <= 0.0f || (Item.Atop && Alpha <= 0.0f)) continue;

                Rgb Fill = Item.Fill;
                const float Depth = -Distance;
                if (Hard) {
                    if (Item.Outlined && Depth < 1.0f / PixelsPerMeter) Fill = scaled(Fill, OutlineShade);
                } else if (Item.Shaded) {
                    // A tube lit from the front: dark at the edge, bright in the middle.
                    const float Edge = 1.0f - std::clamp(Depth / Item.HalfThickness, 0.0f, 1.0f);
                    const float Round = std::sqrt(std::max(1.0f - Edge * Edge, 0.0f));
                    Fill = scaled(Fill, 0.68f + 0.40f * Round);
                }
                if (Item.Atop) {
                    Color = mixed(Color, Fill, Coverage);
                    continue;
                }
                // The "over" operator on straight (not premultiplied) colours.
                const float NewAlpha = Coverage + Alpha * (1.0f - Coverage);
                const float Below = Alpha * (1.0f - Coverage);
                Color = {(Fill.R * Coverage + Color.R * Below) / NewAlpha,
                         (Fill.G * Coverage + Color.G * Below) / NewAlpha,
                         (Fill.B * Coverage + Color.B * Below) / NewAlpha};
                Alpha = NewAlpha;
            }
            if (Alpha <= 0.0f) continue;
            Image.setPixel({Column, Row}, sf::Color(toByte(Color.R), toByte(Color.G), toByte(Color.B), toByte(Alpha)));
        }
    }
    return Image;
}

/// A dome over the upper part of the head with a rim and a nose guard.
std::vector<Layer> makeHelmetLayers(const PartGeometry& Geometry, Rgb Metal) {
    const float Radius = std::min(Geometry.ShapeSize.X, Geometry.ShapeSize.Y) * 0.5f;
    const float RimY = -0.2f * Radius;
    const float DomeRadius = Radius + ArmorMargin;
    const Sdf Dome = [DomeRadius, RimY](Vec2 Point) {
        return std::max(getSdfCircle(Point, {}, DomeRadius), RimY - Point.Y);
    };

    Layer DomeLayer;
    DomeLayer.Shape = Dome;
    DomeLayer.Fill = Metal;
    DomeLayer.HalfThickness = Radius * 0.6f;

    Layer Rim;
    Rim.Shape = [RimY, Radius](Vec2 Point) {
        return getSdfBox(Point, {0.0f, RimY + 0.1f * Radius}, {Radius * 1.2f, 0.1f * Radius}, 0.0f);
    };
    Rim.Atop = true;
    Rim.Fill = scaled(Metal, 0.7f);
    Rim.Outlined = false;
    Rim.Shaded = false;

    Layer NoseGuard;
    const Vec2 GuardCenter{0.72f * Radius, -0.3f * Radius};
    const Vec2 GuardHalf{0.07f * Radius, 0.4f * Radius};
    NoseGuard.Shape = [GuardCenter, GuardHalf](Vec2 Point) { return getSdfBox(Point, GuardCenter, GuardHalf, 0.0f); };
    NoseGuard.Fill = scaled(Metal, 0.85f);
    NoseGuard.HalfThickness = GuardHalf.X;
    return {DomeLayer, Rim, NoseGuard};
}

/// A plate slightly larger than the part it covers.
std::vector<Layer> makeShellLayers(const PartGeometry& Geometry, Rgb Metal) {
    const Sdf Body = makeBodySdf(Geometry);
    Layer Shell;
    Shell.Shape = [Body](Vec2 Point) { return Body(Point) - ArmorMargin; };
    Shell.Fill = Metal;
    Shell.HalfThickness = getHalfThickness(Geometry) + ArmorMargin;
    return {Shell};
}

/// A weapon points down (-Y) with its hilt in the fist and the working end
/// beyond the fist's surface by the reach: a blade for a sword, a head on a
/// shaft for anything blunt.
std::vector<Layer> makeWeaponLayers(const stats::WeaponProps& Weapon, std::string_view MoveSet, float Fist,
                                    float HalfWidth) {
    const float Reach = Weapon.ReachM;
    const Rgb Wood{0.45f, 0.30f, 0.16f};
    const Rgb Gold{0.85f, 0.70f, 0.25f};
    const Rgb Steel{0.82f, 0.85f, 0.90f};
    std::vector<Layer> Layers;

    auto addLayer = [&Layers](Sdf Shape, Rgb Fill, float HalfThickness) {
        Layer Added;
        Added.Shape = std::move(Shape);
        Added.Fill = Fill;
        Added.HalfThickness = HalfThickness;
        Layers.push_back(std::move(Added));
        return Layers.size() - 1;
    };
    auto boxSdf = [](Vec2 Center, Vec2 Half) {
        return [Center, Half](Vec2 Point) { return getSdfBox(Point, Center, Half, 0.0f); };
    };

    if (MoveSet.contains("sword")) {
        const float TipLength = 0.05f;
        const float BladeTop = Fist - 0.016f;
        const float ShoulderY = Fist - Reach + TipLength;
        const Vec2 BladeCenter{0.0f, (BladeTop + ShoulderY) * 0.5f};
        const Vec2 BladeHalf{HalfWidth, (BladeTop - ShoulderY) * 0.5f};
        const Vec2 TipLeft{-HalfWidth, ShoulderY};
        const Vec2 TipRight{HalfWidth, ShoulderY};
        const Vec2 Tip{0.0f, Fist - Reach};
        const Sdf Blade = [=](Vec2 Point) {
            return std::min(getSdfBox(Point, BladeCenter, BladeHalf, 0.0f),
                            getSdfTriangle(Point, TipLeft, TipRight, Tip));
        };
        addLayer(boxSdf({0.0f, Fist + 0.045f}, {0.017f, 0.045f}), Wood, 0.017f);
        addLayer(Blade, Steel, HalfWidth);
        const size_t Fuller =
            addLayer(boxSdf({0.0f, Fist - Reach * 0.5f}, {0.004f, Reach * 0.4f}), scaled(Steel, 0.7f), 0.004f);
        Layers[Fuller].Atop = true;
        Layers[Fuller].Outlined = false;
        Layers[Fuller].Shaded = false;
        addLayer(boxSdf({0.0f, Fist - 0.006f}, {0.06f, 0.011f}), Gold, 0.011f);
        addLayer([Fist](Vec2 Point) { return getSdfCircle(Point, {0.0f, Fist + 0.098f}, 0.022f); }, Gold, 0.022f);
    } else {
        const float HeadAcross = HalfWidth * HeadAcrossShare;
        const float HeadAlong = HalfWidth * HeadAlongShare;
        const float HaftHalf = HalfWidth * HaftShare;
        const float HeadY = Fist - Reach + HeadAlong;
        const float Butt = Fist + HaftTail - 0.03f;
        addLayer(boxSdf({0.0f, (Butt + HeadY) * 0.5f}, {HaftHalf, (Butt - HeadY) * 0.5f}), Wood,
                 HaftHalf);
        addLayer(boxSdf({0.0f, HeadY}, {HeadAcross, HeadAlong}), scaled(Steel, 0.7f), HeadAlong);
        // Striking faces: thicker plates at both ends of the head.
        const float FaceHalf = HeadAcross * 0.18f;
        for (const float Side : {-1.0f, 1.0f}) {
            addLayer(boxSdf({Side * (HeadAcross - FaceHalf), HeadY}, {FaceHalf, HeadAlong * 1.2f}),
                     scaled(Steel, 0.55f), HeadAlong);
        }
        // A band where the haft goes through the head.
        addLayer(boxSdf({0.0f, HeadY}, {HaftHalf * 1.8f, HeadAlong * 1.1f}), Gold, HaftHalf);
        addLayer([Butt](Vec2 Point) { return getSdfCircle(Point, {0.0f, Butt + 0.008f}, 0.022f); }, scaled(Steel, 0.6f),
                 0.022f);
    }
    return Layers;
}

/// A shield is the body's plate: a wooden board, length along the forearm and
/// width across it, centered on the forearm and turned by its angle, with a
/// metal rim and a boss in the middle.
std::vector<Layer> makeShieldLayers(const stats::ShieldProps& Shield) {
    const Rgb Wood{0.55f, 0.36f, 0.18f};
    const Rgb Iron{0.55f, 0.58f, 0.62f};
    const Vec2 Half{Shield.WidthM * 0.5f, Shield.LengthM * 0.5f};
    const float Turn = Shield.AngleDeg * RadiansPerDegree;
    const float Cos = std::cos(Turn);
    const float Sin = std::sin(Turn);
    auto turned = [Cos, Sin](Vec2 Point) { return Vec2{Point.X * Cos + Point.Y * Sin, Point.Y * Cos - Point.X * Sin}; };
    constexpr float Rim = 0.02f;
    constexpr float Boss = 0.05f;

    Layer Board;
    Board.Shape = [=](Vec2 Point) { return getSdfBox(turned(Point), {}, Half - Vec2{Rim, Rim}, Rim); };
    Board.Fill = Iron;
    Board.HalfThickness = std::min(Half.X, Half.Y);
    Layer Planks;
    Planks.Shape = [=](Vec2 Point) { return getSdfBox(turned(Point), {}, Half - Vec2{2.0f * Rim, 2.0f * Rim}, 0.0f); };
    Planks.Fill = Wood;
    Planks.HalfThickness = std::min(Half.X, Half.Y) - Rim;
    Planks.Atop = true;
    Planks.Outlined = false;
    Layer Center;
    Center.Shape = [](Vec2 Point) { return getSdfCircle(Point, {}, Boss); };
    Center.Fill = Iron;
    Center.HalfThickness = Boss;
    Center.Atop = true;
    return {Board, Planks, Center};
}

/// Leather brown for light armor, steel grey for heavy.
Rgb getArmorColor(float Armor) {
    const Rgb Leather{0.50f, 0.33f, 0.18f};
    const Rgb Steel{0.62f, 0.67f, 0.74f};
    return mixed(Leather, Steel, std::clamp(Armor / 0.3f, 0.0f, 1.0f));
}

/// The fighter assembled from the pictures in the reference pose, in the
/// drawing order of docs/ART.md, enlarged to 256 px/m on a gray background.
sf::Image drawReferencePose(const rig::RigDef& Rig, Variant Kind, float Overlap,
                            const std::vector<const stats::EquipmentItem*>& Items) {
    constexpr float SheetPixelsPerMeter = 256.0f;
    constexpr float Left = -0.5f;
    constexpr float Top = 1.95f;
    const float PixelsPerMeter = getDefaultPixelsPerMeter(Kind);
    const auto Scale = static_cast<unsigned>(std::lround(SheetPixelsPerMeter / PixelsPerMeter));

    sf::Image Sheet(sf::Vector2u{static_cast<unsigned>(1.0f * SheetPixelsPerMeter),
                                 static_cast<unsigned>(2.1f * SheetPixelsPerMeter)},
                    sf::Color(205, 205, 210));
    constexpr std::array Order = {BodyPart::UpperArmR, BodyPart::ForearmR, BodyPart::ThighR, BodyPart::ShinR,
                                  BodyPart::FootR,     BodyPart::Pelvis,   BodyPart::Torso,  BodyPart::ThighL,
                                  BodyPart::ShinL,     BodyPart::FootL,    BodyPart::Head,   BodyPart::UpperArmL,
                                  BodyPart::ForearmL};
    for (const BodyPart Part : Order) {
        const auto Geometry = computePartGeometry(Rig.getPart(Part), Overlap);
        const sf::Vector2i Center{static_cast<int>((Geometry.Center.X - Left) * SheetPixelsPerMeter),
                                  static_cast<int>((Top - Geometry.Center.Y) * SheetPixelsPerMeter)};
        blit(Sheet, drawPart(Geometry, Kind), Center, Scale);
    }
    // The items go on top of the whole body: in the reference pose the arms
    // hang over the legs, and a weapon behind a leg would not be seen.
    for (const BodyPart Part : Order) {
        const auto Geometry = computePartGeometry(Rig.getPart(Part), Overlap);
        const sf::Vector2i Center{static_cast<int>((Geometry.Center.X - Left) * SheetPixelsPerMeter),
                                  static_cast<int>((Top - Geometry.Center.Y) * SheetPixelsPerMeter)};
        for (const stats::EquipmentItem* Item : Items) {
            if (std::ranges::find(Item->Covers, Part) != Item->Covers.end()) {
                blit(Sheet, drawItemOverlay(*Item, Geometry, Kind), Center, Scale);
            }
        }
    }
    return Sheet;
}

/// Alpha-blends \p Source, enlarged \p Scale times without smoothing, onto
/// \p Target so that its center lands on \p Center.
void blit(sf::Image& Target, const sf::Image& Source, sf::Vector2i Center, unsigned Scale) {
    const auto Size = Source.getSize();
    const int Width = static_cast<int>(Size.x * Scale);
    const int Height = static_cast<int>(Size.y * Scale);
    const auto TargetSize = Target.getSize();
    for (int Row = 0; Row < Height; ++Row) {
        for (int Column = 0; Column < Width; ++Column) {
            const int X = Center.x - Width / 2 + Column;
            const int Y = Center.y - Height / 2 + Row;
            if (X < 0 || Y < 0 || X >= static_cast<int>(TargetSize.x) || Y >= static_cast<int>(TargetSize.y)) continue;
            const sf::Vector2u From{static_cast<unsigned>(Column) / Scale, static_cast<unsigned>(Row) / Scale};
            const sf::Color Over = Source.getPixel(From);
            if (Over.a == 0) continue;
            const sf::Vector2u At{static_cast<unsigned>(X), static_cast<unsigned>(Y)};
            const sf::Color Under = Target.getPixel(At);
            const auto Mix = [&Over](uint8_t Below, uint8_t Above) {
                return static_cast<uint8_t>((Above * Over.a + Below * (255 - Over.a)) / 255);
            };
            Target.setPixel(At, sf::Color(Mix(Under.r, Over.r), Mix(Under.g, Over.g), Mix(Under.b, Over.b)));
        }
    }
}

std::vector<std::string> readVisualItemIds(const std::filesystem::path& VisualsPath) {
    if (!std::filesystem::exists(VisualsPath)) return {};
    std::vector<std::string> Ids;
    try {
        const Json Root = Json::parse(readTextFile(VisualsPath));
        const auto Found = Root.find("items");
        if (Found == Root.end()) return {};
        for (const auto& [Key, Value] : Found->items()) Ids.push_back(Key);
    } catch (const Json::exception& Error) {
        throw std::runtime_error(std::format("{}: {}", VisualsPath.string(), Error.what()));
    }
    return Ids;
}

void saveImage(const sf::Image& Image, const std::filesystem::path& Path) {
    std::filesystem::create_directories(Path.parent_path());
    if (!Image.saveToFile(Path)) throw std::runtime_error(std::format("cannot write {}", Path.string()));
}

} // namespace

} // namespace fighter::tools
