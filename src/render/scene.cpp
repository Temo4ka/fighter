#include "render/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>

namespace fighter::render {
namespace {

const std::array<sf::Color, 2> FighterColors = {sf::Color(200, 70, 60), sf::Color(60, 110, 200)};
const sf::Color StaminaColor(225, 195, 70);
const sf::Color SkyColor(70, 80, 100);
const sf::Color FloorColor(45, 40, 38);
const sf::Color WallColor(30, 28, 27, 200);
/// Width of a wall as drawn; the wall itself is the line at +-HalfWidthM.
constexpr float WallThicknessM = 0.15f;

constexpr std::array<BodyPart, BodyPartCount> PartDrawOrder = {
    BodyPart::UpperArmR, BodyPart::ForearmR,
    BodyPart::ThighR, BodyPart::ShinR, BodyPart::FootR,
    BodyPart::Pelvis, BodyPart::Torso,
    BodyPart::ThighL, BodyPart::ShinL, BodyPart::FootL,
    BodyPart::Head,
    BodyPart::UpperArmL, BodyPart::ForearmL,
};

void appendArena(RenderList& List, const combat::RenderSnapshot& Snapshot, const SceneInput& Scene);
void appendFighter(RenderList& List, Layer Where, const combat::FighterView& Fighter, const FighterSprites* Sprites,
                   sf::Color Color);
void appendHud(RenderList& List, const combat::RenderSnapshot& Snapshot, const HudParams& Hud,
               const SceneInput& Scene);
bool isFarSide(BodyPart Part);
sf::Color darken(sf::Color Color);
float getRatio(float Value, float Max);

} // namespace

std::span<const BodyPart, BodyPartCount> getPartDrawOrder() { return PartDrawOrder; }

RenderList buildRenderList(const combat::RenderSnapshot& Snapshot, const Visuals& Vis, const SceneInput& Scene) {
    RenderList List;
    appendArena(List, Snapshot, Scene);

    const size_t Far = std::min<size_t>(Scene.FarFighter, Snapshot.Fighters.size() - 1);
    for (size_t Index = 0; Index < Snapshot.Fighters.size(); ++Index) {
        const Layer Where = Index == Far ? Layer::FarFighter : Layer::NearFighter;
        appendFighter(List, Where, Snapshot.Fighters[Index], Scene.Sprites[Index], FighterColors[Index]);
    }

    if (Scene.Effects) Scene.Effects->appendTo(List, Scene.FrameTick, Vis.Effects);
    appendHud(List, Snapshot, Vis.Hud, Scene);
    return List;
}

namespace {

void appendArena(RenderList& List, const combat::RenderSnapshot& Snapshot, const SceneInput& Scene) {
    const Vec2 View = Scene.ViewSizeM;
    if (const sf::Texture* Texture = Scene.Background; Texture && Texture->getSize().x > 0 && Texture->getSize().y > 0) {
        // Scaled with its aspect ratio kept so that it covers the whole view.
        const float Scale = std::max(View.X / static_cast<float>(Texture->getSize().x),
                                     View.Y / static_cast<float>(Texture->getSize().y));
        List.add(Layer::Background, SpritePrim{.Texture = Texture, .Position = Scene.ViewCenterM,
                                               .Scale = {Scale, Scale}});
    } else {
        List.add(Layer::Background, RectPrim{.Position = Scene.ViewCenterM, .Size = View, .Fill = SkyColor});
    }

    // The floor: a band below y = 0 across the whole view, however it moves.
    const float Depth = View.Y;
    List.add(Layer::Arena, RectPrim{.Position = {Scene.ViewCenterM.X, -Depth * 0.5f},
                                    .Size = {View.X * 2.0f, Depth},
                                    .Fill = FloorColor});

    // The walls: from the floor to above the view, outside the arena.
    const float HalfWidth = Snapshot.Arena.HalfWidthM;
    const float Height = Scene.ViewCenterM.Y + View.Y;
    for (const float Side : {-1.0f, 1.0f}) {
        List.add(Layer::Arena, RectPrim{.Position = {Side * (HalfWidth + WallThicknessM * 0.5f), Height * 0.5f},
                                        .Size = {WallThicknessM, Height},
                                        .Fill = WallColor});
    }
}

void appendFighter(RenderList& List, Layer Where, const combat::FighterView& Fighter, const FighterSprites* Sprites,
                   sf::Color Color) {
    if (Fighter.Parts.empty()) {
        // No body yet: one rectangle of the bounding size.
        List.add(Where, RectPrim{.Position = Fighter.Position + Vec2{0.0f, Fighter.Size.Y * 0.5f},
                                 .Size = Fighter.Size,
                                 .Fill = Color,
                                 .Outline = sf::Color::Black,
                                 .OutlineThickness = 0.02f});
        return;
    }

    PerBodyPart<const PartTransform*> ByPart{};
    for (const PartTransform& Part : Fighter.Parts) ByPart[static_cast<size_t>(Part.Part)] = &Part;

    // A fighter facing left is the mirror image of one facing right: the rig
    // mirrors the placement, the picture is mirrored across its own Y axis.
    const float Mirror = Fighter.FacingRight ? 1.0f : -1.0f;
    for (const BodyPart Part : PartDrawOrder) {
        const PartTransform* Transform = ByPart[static_cast<size_t>(Part)];
        if (!Transform) continue;

        const SpriteRef* Picture = Sprites ? &Sprites->Parts[static_cast<size_t>(Part)] : nullptr;
        if (Picture && Picture->Texture) {
            List.add(Where, SpritePrim{.Texture = Picture->Texture,
                                       .Position = Transform->Position,
                                       .Angle = Transform->Angle,
                                       .Scale = {Picture->MetersPerPixel * Mirror, Picture->MetersPerPixel},
                                       .Origin = Picture->Origin});
        } else {
            List.addFallback(Where, CapsulePrim{.Position = Transform->Position,
                                                .Angle = Transform->Angle,
                                                .Size = Transform->Size,
                                                .Fill = isFarSide(Part) ? darken(Color) : Color});
        }

        if (!Sprites) continue;
        for (const SpriteRef& Overlay : Sprites->Overlays[static_cast<size_t>(Part)]) {
            List.add(Where, SpritePrim{.Texture = Overlay.Texture,
                                       .Position = Transform->Position,
                                       .Angle = Transform->Angle,
                                       .Scale = {Overlay.MetersPerPixel * Mirror, Overlay.MetersPerPixel},
                                       .Origin = Overlay.Origin});
        }
    }
}

void appendHud(RenderList& List, const combat::RenderSnapshot& Snapshot, const HudParams& Hud,
               const SceneInput& Scene) {
    const float Width = Scene.ScreenSizePx.X;
    for (size_t Index = 0; Index < Snapshot.Fighters.size(); ++Index) {
        const combat::FighterView& Fighter = Snapshot.Fighters[Index];
        const bool Right = Index == 1;
        // The left fighter's bars on the left, the right one's on the right;
        // the right bars shrink towards the screen center, as in classic fighting games.
        const float BarX = Right ? Width - Hud.MarginPx - Hud.BarWidthPx : Hud.MarginPx;
        float Y = Hud.MarginPx;

        List.add(Layer::Hud, BarPrim{.Position = {BarX, Y},
                                     .Size = {Hud.BarWidthPx, Hud.HpBarHeightPx},
                                     .Ratio = getRatio(Fighter.Hp, Fighter.MaxHp),
                                     .FillFromRight = Right,
                                     .Fill = FighterColors[Index]});
        Y += Hud.HpBarHeightPx + Hud.GapPx;

        if (Hud.StaminaBarHeightPx > 0.0f) {
            List.add(Layer::Hud, BarPrim{.Position = {BarX, Y},
                                         .Size = {Hud.BarWidthPx, Hud.StaminaBarHeightPx},
                                         .Ratio = getRatio(Fighter.Stamina, Fighter.MaxStamina),
                                         .FillFromRight = Right,
                                         .Fill = StaminaColor});
            Y += Hud.StaminaBarHeightPx + Hud.GapPx;
        }

        List.add(Layer::Hud, TextPrim{.Text = Scene.Names[Index],
                                      .Position = {Right ? BarX + Hud.BarWidthPx : BarX, Y},
                                      .SizePx = Hud.NameFontPx,
                                      .AlignX = Right ? 1.0f : 0.0f});
    }

    // One round (O.5): the time left, no round counter.
    const double Seconds = std::max(std::ceil(Snapshot.TimeLeftSec), 0.0);
    List.add(Layer::Hud, TextPrim{.Text = std::format("{:.0f}", Seconds),
                                  .Position = {Width * 0.5f, std::max(Hud.MarginPx - 6.0f, 0.0f)},
                                  .SizePx = Hud.TimerFontPx,
                                  .AlignX = 0.5f});
}

/// Parts of the far side (the "R" limbs of a fighter seen from its left) are
/// darker, so the near limbs read as being in front.
bool isFarSide(BodyPart Part) {
    switch (Part) {
        case BodyPart::UpperArmR:
        case BodyPart::ForearmR:
        case BodyPart::ThighR:
        case BodyPart::ShinR:
        case BodyPart::FootR:
            return true;
        default:
            return false;
    }
}

sf::Color darken(sf::Color Color) {
    return sf::Color(static_cast<uint8_t>(Color.r * 3 / 5), static_cast<uint8_t>(Color.g * 3 / 5),
                     static_cast<uint8_t>(Color.b * 3 / 5), Color.a);
}

float getRatio(float Value, float Max) { return Max > 0.0f ? std::clamp(Value / Max, 0.0f, 1.0f) : 0.0f; }

} // namespace

} // namespace fighter::render
