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

GhostContext loadGhostContext(const std::filesystem::path& Root, const std::string& ItemId) {
    const std::filesystem::path DataDir = Root / "data";
    GhostContext Context;
    Context.Rig = rig::loadRigDef(DataDir / "rigs" / "humanoid.json");

    const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
    const combat::MoveLibrary Library = combat::MoveLibrary::load(DataDir);
    stats::Loadout Gear;
    if (!ItemId.empty()) {
        if (!Catalog.findItem(ItemId)) {
            throw std::runtime_error(std::format("unknown item '{}': there is no such id in {}", ItemId,
                                                 (DataDir / "items").string()));
        }
        Gear = stats::buildLoadout(std::vector<std::string>{ItemId}, Catalog);
        Context.ItemName = Catalog.findItem(ItemId)->Name;
    }
    Context.Held = combat::getHeldItems(Gear, Context.Rig.Weapon);
    if (Gear.findWeapon()) Context.WeaponHand = Context.Rig.Weapon.Part;

    const combat::MoveSet& Set = Library.selectSet(Gear.getMoveSet(stats::EquipmentSlot::MainHand),
                                                   Gear.getMoveSet(stats::EquipmentSlot::OffHand), false);
    const std::string StanceName = Library.getStance(Set, combat::clips::Stance);
    Context.Stance = anim::loadClip(DataDir / "poses" / (StanceName + ".json"));
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
