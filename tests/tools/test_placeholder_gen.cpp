#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

#include "placeholder_gen/placeholder_gen.hpp"
#include "rig/rig_def.hpp"
#include "stats/loading.hpp"

using namespace fighter;
using namespace fighter::tools;
using Catch::Approx;

namespace {

const std::filesystem::path DataDir = std::filesystem::path(FIGHTER_DATA_DIR);

rig::RigDef loadHumanoid() { return rig::loadRigDef(DataDir / "rigs" / "humanoid.json"); }

rig::PartDef makeCircle(float Radius) {
    rig::PartDef Part;
    Part.Part = BodyPart::Head;
    Part.Shape = physics::ShapeKind::Circle;
    Part.Center = {0.01f, 1.65f};
    Part.Radius = Radius;
    return Part;
}

rig::PartDef makeCapsule() {
    rig::PartDef Part;
    Part.Part = BodyPart::ThighL;
    Part.Shape = physics::ShapeKind::Capsule;
    Part.Begin = {0.0f, 0.88f};
    Part.End = {0.0f, 0.56f};
    Part.Radius = 0.07f;
    return Part;
}

rig::PartDef makeBox(Vec2 HalfExtents, float Rounding) {
    rig::PartDef Part;
    Part.Part = BodyPart::Pelvis;
    Part.Shape = physics::ShapeKind::Box;
    Part.Center = {0.0f, 0.98f};
    Part.HalfExtents = HalfExtents;
    Part.Radius = Rounding;
    return Part;
}

bool isFlippedEqual(const sf::Image& Image, bool Horizontal) {
    const auto Size = Image.getSize();
    for (unsigned Row = 0; Row < Size.y; ++Row) {
        for (unsigned Column = 0; Column < Size.x; ++Column) {
            const unsigned FlippedColumn = Horizontal ? Size.x - 1 - Column : Column;
            const unsigned FlippedRow = Horizontal ? Row : Size.y - 1 - Row;
            if (Image.getPixel({Column, Row}).a != Image.getPixel({FlippedColumn, FlippedRow}).a) return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("computePartGeometry: sizes of a circle, a capsule and a box", "[placeholder]") {
    SECTION("a circle: diameter, overlap along Y") {
        const auto Geometry = computePartGeometry(makeCircle(0.11f), 0.15f);
        CHECK(Geometry.ShapeSize.X == Approx(0.22f));
        CHECK(Geometry.ShapeSize.Y == Approx(0.22f));
        CHECK(Geometry.Padding.X == 0.0f);
        CHECK(Geometry.Padding.Y == Approx(0.033f));
        CHECK(Geometry.getImageSize().Y == Approx(0.286f));
    }
    SECTION("a capsule: segment plus both caps, overlap at both ends") {
        const auto Geometry = computePartGeometry(makeCapsule(), 0.15f);
        CHECK(Geometry.ShapeSize.X == Approx(0.14f));
        CHECK(Geometry.ShapeSize.Y == Approx(0.46f));
        CHECK(Geometry.Padding.Y == Approx(0.069f));
        CHECK(Geometry.getImageSize().X == Approx(0.14f));
        CHECK(Geometry.getImageSize().Y == Approx(0.598f));
    }
    SECTION("a box: half extents plus rounding on every side") {
        const auto Geometry = computePartGeometry(makeBox({0.11f, 0.08f}, 0.03f), 0.15f);
        CHECK(Geometry.ShapeSize.X == Approx(0.28f));
        CHECK(Geometry.ShapeSize.Y == Approx(0.22f));
        // Not much wider than tall: stands, so the overlap is along Y.
        CHECK(Geometry.Padding.X == 0.0f);
        CHECK(Geometry.Padding.Y == Approx(0.033f));
    }
    SECTION("a flat box lies down: the overlap is along X") {
        const auto Geometry = computePartGeometry(makeBox({0.12f, 0.04f}, 0.01f), 0.15f);
        CHECK(Geometry.ShapeSize.X == Approx(0.26f));
        CHECK(Geometry.ShapeSize.Y == Approx(0.10f));
        CHECK(Geometry.Padding.X == Approx(0.039f));
        CHECK(Geometry.Padding.Y == 0.0f);
        CHECK(Geometry.getImageSize().X == Approx(0.338f));
    }
    SECTION("the overlap parameter is a share of the length") {
        const auto None = computePartGeometry(makeCapsule(), 0.0f);
        CHECK(None.getImageSize().Y == Approx(0.46f));
        const auto Double = computePartGeometry(makeCapsule(), 0.30f);
        CHECK(Double.Padding.Y == Approx(0.138f));
    }
    SECTION("by default the picture is the shape; the head keeps room for the neck") {
        const auto Limb = computePartGeometry(makeCapsule());
        CHECK(Limb.getImageSize().X == Approx(Limb.ShapeSize.X));
        CHECK(Limb.getImageSize().Y == Approx(Limb.ShapeSize.Y));
        const auto Foot = computePartGeometry(makeBox({0.12f, 0.04f}, 0.01f));
        CHECK(Foot.getImageSize().X == Approx(Foot.ShapeSize.X));
        const auto Head = computePartGeometry(makeCircle(0.11f));
        CHECK(Head.Padding.Y == Approx(0.033f));
    }
}

TEST_CASE("getImageSizePixels: both densities", "[placeholder]") {
    // Table of docs/ART.md: forearm 0.09 x 0.35 m is 6 x 22 px at 64 px/m
    // without overlap.
    CHECK(getImageSizePixels(0.35f, 64.0f) == 23);
    CHECK(getImageSizePixels(0.35f, 32.0f) == 12);
    // Exact multiples are not rounded up by float noise.
    CHECK(getImageSizePixels(0.25f, 64.0f) == 16);
    CHECK(getImageSizePixels(0.25f, 32.0f) == 8);
    CHECK(getImageSizePixels(0.0f, 64.0f) == 1);

    const auto Geometry = computePartGeometry(makeCapsule(), 0.15f);
    const sf::Image Smooth = drawPart(Geometry, Variant::Smooth);
    const sf::Image Pixel = drawPart(Geometry, Variant::Pixel);
    CHECK(Smooth.getSize().x == getImageSizePixels(0.14f, 64.0f));
    CHECK(Smooth.getSize().y == getImageSizePixels(0.598f, 64.0f));
    CHECK(Pixel.getSize().x == getImageSizePixels(0.14f, 32.0f));
    CHECK(Pixel.getSize().y == getImageSizePixels(0.598f, 32.0f));
    CHECK(getDefaultPixelsPerMeter(Variant::Smooth) == 2.0f * getDefaultPixelsPerMeter(Variant::Pixel));
}

TEST_CASE("computePartGeometry: the picture is centred on the shape centre", "[placeholder]") {
    const auto Rig = loadHumanoid();
    for (const auto& Part : Rig.Parts) {
        INFO(getBodyPartName(Part.Part));
        const auto Geometry = computePartGeometry(Part);
        const bool IsCapsule = Part.Shape == physics::ShapeKind::Capsule;
        const Vec2 ShapeCenter = IsCapsule ? (Part.Begin + Part.End) * 0.5f : Part.Center;
        CHECK(Geometry.Center.X == Approx(ShapeCenter.X));
        CHECK(Geometry.Center.Y == Approx(ShapeCenter.Y));
        // Any padding is equal at both ends, so the picture contains the shape
        // with the same margin on both sides.
        CHECK(Geometry.getImageSize().X >= Geometry.ShapeSize.X);
        CHECK(Geometry.getImageSize().Y >= Geometry.ShapeSize.Y);
    }
}

TEST_CASE("drawPart: the drawn silhouette is symmetric about the image center", "[placeholder]") {
    const auto Rig = loadHumanoid();
    for (const Variant Kind : {Variant::Pixel, Variant::Smooth}) {
        for (const auto& Part : Rig.Parts) {
            INFO(getBodyPartName(Part.Part) << " " << getVariantName(Kind));
            const sf::Image Image = drawPart(computePartGeometry(Part), Kind);
            CHECK(isFlippedEqual(Image, true));
            // The head has a neck stub at the bottom only.
            if (Part.Part != BodyPart::Head) CHECK(isFlippedEqual(Image, false));
        }
    }
}

TEST_CASE("drawPart: expected size, transparent corners, opaque center", "[placeholder]") {
    const auto Rig = loadHumanoid();
    for (const Variant Kind : {Variant::Pixel, Variant::Smooth}) {
        const float Ppm = getDefaultPixelsPerMeter(Kind);
        for (const auto& Part : Rig.Parts) {
            INFO(getBodyPartName(Part.Part) << " " << getVariantName(Kind));
            const auto Geometry = computePartGeometry(Part);
            const sf::Image Image = drawPart(Geometry, Kind);
            const auto Size = Image.getSize();
            CHECK(Size.x == getImageSizePixels(Geometry.getImageSize().X, Ppm));
            CHECK(Size.y == getImageSizePixels(Geometry.getImageSize().Y, Ppm));

            // Rounded boxes of a few pixels are almost square: only the round
            // shapes must have empty corners.
            if (Part.Shape != physics::ShapeKind::Box) {
                CHECK(Image.getPixel({0, 0}).a == 0);
                CHECK(Image.getPixel({Size.x - 1, 0}).a == 0);
                CHECK(Image.getPixel({0, Size.y - 1}).a == 0);
                CHECK(Image.getPixel({Size.x - 1, Size.y - 1}).a == 0);
            }
            CHECK(Image.getPixel({Size.x / 2, Size.y / 2}).a == 255);
        }
    }
}

TEST_CASE("drawPart: near parts are lighter than far parts", "[placeholder]") {
    const auto Rig = loadHumanoid();
    auto getBrightness = [&](BodyPart Part) {
        const sf::Image Image = drawPart(computePartGeometry(Rig.getPart(Part)), Variant::Pixel);
        const auto Size = Image.getSize();
        // A column off the stripe mark, in the middle of the limb.
        const sf::Color Pixel = Image.getPixel({Size.x / 2 - 1, Size.y / 2});
        return Pixel.r + Pixel.g + Pixel.b;
    };
    CHECK(getBrightness(BodyPart::UpperArmL) > getBrightness(BodyPart::UpperArmR));
    CHECK(getBrightness(BodyPart::ThighL) > getBrightness(BodyPart::ThighR));
}

TEST_CASE("drawItemOverlay: a sword extends past the fist by its reach", "[placeholder]") {
    const auto Rig = loadHumanoid();
    const auto Catalog = stats::loadItemCatalog(DataDir / "items");
    const auto* Sword = Catalog.findItem("short_sword");
    REQUIRE(Sword != nullptr);
    REQUIRE(Sword->Weapon.has_value());

    const auto Geometry = computePartGeometry(Rig.getPart(BodyPart::ForearmR));
    for (const Variant Kind : {Variant::Pixel, Variant::Smooth}) {
        INFO(getVariantName(Kind));
        const float Ppm = getDefaultPixelsPerMeter(Kind);
        const sf::Image Overlay = drawItemOverlay(*Sword, Geometry, Kind);
        const sf::Image Forearm = drawPart(Geometry, Kind);
        const auto Size = Overlay.getSize();

        // Centred on the forearm centre: the picture is symmetric in height and
        // taller than the forearm by twice the reach.
        const float ExpectedHeight = Geometry.getImageSize().Y + 2.0f * (Sword->Weapon->ReachM + 0.012f);
        CHECK(Size.y == getImageSizePixels(ExpectedHeight, Ppm));
        CHECK(Size.y > Forearm.getSize().y);

        // A pixel on the blade axis, 80% of the reach below the fist edge, is
        // drawn, and it lies below the end of the forearm picture itself.
        const float FistY = Geometry.ShapeSize.Y * 0.5f;
        const float BladeY = FistY + 0.8f * Sword->Weapon->ReachM;
        const auto Row = static_cast<unsigned>(static_cast<float>(Size.y) * 0.5f + BladeY * Ppm);
        CHECK(Overlay.getPixel({Size.x / 2, Row}).a > 0);
        CHECK(BladeY > Geometry.getImageSize().Y * 0.5f);
        // The upper half has the hilt only, nothing above the pommel.
        CHECK(Overlay.getPixel({Size.x / 2, 0}).a == 0);
    }
}

TEST_CASE("drawItemOverlay: a helmet covers the top of the head", "[placeholder]") {
    const auto Rig = loadHumanoid();
    const auto Catalog = stats::loadItemCatalog(DataDir / "items");
    const auto* Helmet = Catalog.findItem("iron_helmet");
    REQUIRE(Helmet != nullptr);

    const auto Geometry = computePartGeometry(Rig.getPart(BodyPart::Head));
    const sf::Image Overlay = drawItemOverlay(*Helmet, Geometry, Variant::Smooth);
    const auto Size = Overlay.getSize();
    CHECK(Overlay.getPixel({Size.x / 2, Size.y / 4}).a == 255);
    // The neck padding at the bottom stays free.
    CHECK(Overlay.getPixel({Size.x / 2, Size.y - 1}).a == 0);
}

TEST_CASE("generatePlaceholders: writes every part and the items of visuals.json", "[placeholder]") {
    const auto Out = std::filesystem::temp_directory_path() / "fighter_placeholder_gen_test";
    std::filesystem::remove_all(Out);

    GenerateOptions Options;
    Options.DataDir = DataDir;
    Options.OutDir = Out;
    const auto Files = generatePlaceholders(Options);

    // 13 parts, a helmet on the head and a sword on either forearm (an item
    // held in a hand may be in either), two variants.
    CHECK(Files.size() == 2 * (BodyPartCount + 3));
    for (const auto* Dir : {"pixel", "smooth"}) {
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const auto Name = std::string(getBodyPartName(static_cast<BodyPart>(Index))) + ".png";
            CHECK(std::filesystem::exists(Out / Dir / "humanoid" / Name));
        }
        CHECK(std::filesystem::exists(Out / Dir / "items" / "iron_helmet" / "Head.png"));
        CHECK(std::filesystem::exists(Out / Dir / "items" / "short_sword" / "ForearmR.png"));
    }

    sf::Image Loaded;
    REQUIRE(Loaded.loadFromFile(Out / "smooth" / "humanoid" / "Torso.png"));
    const auto Geometry = computePartGeometry(loadHumanoid().getPart(BodyPart::Torso));
    CHECK(Loaded.getSize().x == getImageSizePixels(Geometry.getImageSize().X, 64.0f));
    CHECK(Loaded.getSize().y == getImageSizePixels(Geometry.getImageSize().Y, 64.0f));

    std::filesystem::remove_all(Out);
}

TEST_CASE("generatePlaceholders: an unknown rig is an error naming the file", "[placeholder]") {
    GenerateOptions Options;
    Options.DataDir = DataDir;
    Options.OutDir = std::filesystem::temp_directory_path() / "fighter_placeholder_gen_missing";
    Options.RigName = "no_such_rig";
    CHECK_THROWS_WITH(generatePlaceholders(Options), Catch::Matchers::ContainsSubstring("no_such_rig.json"));
}
