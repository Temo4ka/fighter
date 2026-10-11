#include "editor/ghost.hpp"

#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "anim/layers.hpp"
#include "combat/clip_library.hpp"
#include "combat/held_items.hpp"
#include "combat/moveset.hpp"
#include "stats/equipment.hpp"
#include "stats/loading.hpp"

namespace fighter::editor {

StanceLookup findStance(const combat::MoveLibrary& Library, const stats::Loadout& Gear) {
    const combat::MoveSet& Set = Library.selectSet(Gear.getMoveSet(stats::EquipmentSlot::MainHand),
                                                   Gear.getMoveSet(stats::EquipmentSlot::OffHand), false);
    return {.MoveSetId = Set.Id, .StanceClip = Library.getStance(Set, combat::clips::Stance)};
}

GhostContext loadGhostContext(const std::filesystem::path& Root, const std::string& ItemId) {
    std::vector<stats::ItemRef> Items;
    if (!ItemId.empty()) Items.push_back({.Id = ItemId, .Slot = std::nullopt});
    return loadGhostContext(Root, Items);
}

GhostContext loadGhostContext(const std::filesystem::path& Root, std::span<const stats::ItemRef> Items) {
    const std::filesystem::path DataDir = Root / "data";
    GhostContext Context;
    Context.Rig = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");
    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const combat::MoveLibrary Library = combat::MoveLibrary::load(DataDir);
    for (const stats::ItemRef& Item : Items) {
        const stats::EquipmentItem* Found = Catalog.findItem(Item.Id);
        if (!Found) {
            throw std::runtime_error(std::format("unknown item '{}': there is no such id in {}", Item.Id,
                                                 (DataDir / "items").string()));
        }
        Context.ItemName += (Context.ItemName.empty() ? "" : " + ") + Found->Name;
    }
    const stats::Loadout Gear = stats::buildLoadout(Items, Catalog);
    Context.Held = combat::getHeldItems(Gear, Context.Rig.Weapon);
    if (Gear.findWeapon()) Context.WeaponHand = Context.Rig.Weapon.Part;
    if (const stats::EquipmentItem* Main = Gear.findInSlot(stats::EquipmentSlot::MainHand);
        Main && Main->TwoHanded && Context.WeaponHand) {
        // As the battle sets up the grip (combat makeRigSetup, rig createGrip).
        Context.GripHand = Context.Rig.Weapon.OffPart;
        Context.GripM = Context.Rig.Weapon.Grip;
        for (const auto& Item : Context.Held) {
            if (Item.Part == *Context.WeaponHand && Item.GripM) Context.GripM = *Item.GripM;
        }
    }
    const StanceLookup Stance = findStance(Library, Gear);
    Context.MoveSetId = Stance.MoveSetId;
    Context.StanceName = Stance.StanceClip;
    Context.Stance = anim::loadClip(DataDir / "poses" / (Stance.StanceClip + ".json"));
    return Context;
}

anim::Clip getPlayedClip(const anim::Clip& Authored, std::optional<BodyPart> WeaponHand) {
    return isPlayedOtherHand(Authored, WeaponHand) ? anim::mirrorClipArms(Authored) : Authored;
}

bool isPlayedOtherHand(const anim::Clip& Authored, std::optional<BodyPart> WeaponHand) {
    if (!WeaponHand || Authored.isStriker(*WeaponHand)) return false;
    return Authored.isStriker(anim::getMirroredArmPart(*WeaponHand));
}

anim::Pose composeGhostPose(const anim::Clip& Stance, const anim::Clip& Edited, float TimeSec,
                            std::optional<BodyPart> WeaponHand) {
    anim::Pose Result = anim::sampleClip(Stance, 0.0f);
    anim::layerPose(Result, anim::sampleClip(getPlayedClip(Edited, WeaponHand), TimeSec));
    return Result;
}

} // namespace fighter::editor
