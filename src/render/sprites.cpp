#include "render/sprites.hpp"

#include <format>
#include <string>
#include <utility>

#include "core/log.hpp"

namespace fighter::render {
namespace {

std::string joinNames(const std::vector<BodyPart>& Parts);
const SkinDef* findSkin(const Visuals& Vis, const FighterLook& Look);
void resolveItem(const Visuals& Vis, const SkinDef* Skin, const ItemLook& Item, const TextureLoader& Load,
                 FighterSprites& Out);

} // namespace

FighterLook makeFighterLook(const combat::FighterConfig& Config, std::string Skin, std::string_view FallbackName) {
    FighterLook Look;
    Look.Name = Config.Name.empty() ? std::string(FallbackName) : Config.Name;
    Look.Skin = std::move(Skin);
    for (const auto& Item : Config.Loadout.Items) Look.Items.push_back({Item.Id, Item.Covers});
    return Look;
}

std::string getPicturePath(std::string_view Dir, BodyPart Part) {
    return std::format("{}/{}.png", Dir, getBodyPartName(Part));
}

FighterSprites resolveFighterSprites(const Visuals& Vis, const FighterLook& Look, const TextureLoader& Load) {
    FighterSprites Out;
    const SkinDef* Skin = findSkin(Vis, Look);
    if (Skin) {
        std::vector<BodyPart> Missing;
        for (size_t Index = 0; Index < BodyPartCount; ++Index) {
            const auto Part = static_cast<BodyPart>(Index);
            SpriteRef& Ref = Out.Parts[Index];
            Ref.Texture = Load(getPicturePath(Skin->Dir, Part), Skin->Smooth);
            Ref.MetersPerPixel = 1.0f / Skin->PixelsPerMeter;
            if (!Ref.Texture) Missing.push_back(Part);
        }
        Out.MissingFiles += Missing.size();
        if (Missing.size() == BodyPartCount) {
            log::warn("{}: no body part pictures in {}, drawing capsules", Look.Name, Skin->Dir);
        } else if (!Missing.empty()) {
            log::warn("{}: no pictures for {} in {}, drawing capsules", Look.Name, joinNames(Missing), Skin->Dir);
        }
    }
    for (const ItemLook& Item : Look.Items) resolveItem(Vis, Skin, Item, Load, Out);
    return Out;
}

namespace {

std::string joinNames(const std::vector<BodyPart>& Parts) {
    std::string Text;
    for (const BodyPart Part : Parts) {
        if (!Text.empty()) Text += ", ";
        Text += getBodyPartName(Part);
    }
    return Text;
}

const SkinDef* findSkin(const Visuals& Vis, const FighterLook& Look) {
    const std::string& Id = Look.Skin.empty() ? Vis.DefaultSkin : Look.Skin;
    if (Id.empty()) {
        log::warn("{}: no skin chosen and no default_skin in visuals, drawing capsules", Look.Name);
        return nullptr;
    }
    const auto Found = Vis.Skins.find(Id);
    if (Found == Vis.Skins.end()) {
        log::warn("{}: unknown skin '{}', drawing capsules", Look.Name, Id);
        return nullptr;
    }
    return &Found->second;
}

void resolveItem(const Visuals& Vis, const SkinDef* Skin, const ItemLook& Item, const TextureLoader& Load,
                 FighterSprites& Out) {
    const auto Override = Vis.Items.find(Item.Id);
    const ItemVisualDef* Def = Override != Vis.Items.end() ? &Override->second : nullptr;

    std::string Dir;
    if (Def && !Def->Dir.empty()) {
        Dir = Def->Dir;
    } else if (Skin && !Skin->ItemsDir.empty()) {
        Dir = std::format("{}/{}", Skin->ItemsDir, Item.Id);
    } else {
        log::debug("item {}: no pictures configured", Item.Id);
        return;
    }

    const float PixelsPerMeter =
        Def && Def->PixelsPerMeter ? *Def->PixelsPerMeter : (Skin ? Skin->PixelsPerMeter : Vis.PixelsPerMeter);
    const bool Smooth = Skin ? Skin->Smooth : true;
    std::vector<BodyPart> Missing;
    for (const BodyPart Part : Item.Covers) {
        SpriteRef Ref;
        Ref.Texture = Load(getPicturePath(Dir, Part), Smooth);
        Ref.MetersPerPixel = 1.0f / PixelsPerMeter;
        if (Def) {
            if (const auto Origin = Def->Origins.find(Part); Origin != Def->Origins.end()) Ref.Origin = Origin->second;
        }
        if (!Ref.Texture) {
            Missing.push_back(Part);
            continue;
        }
        Out.Overlays[static_cast<size_t>(Part)].push_back(Ref);
    }
    Out.MissingFiles += Missing.size();
    if (!Missing.empty()) log::warn("item {}: no pictures for {} in {}, not drawn", Item.Id, joinNames(Missing), Dir);
}

} // namespace

} // namespace fighter::render
