#include "ui/fighter_card.hpp"

#include <exception>

#include "stats/equipment.hpp"
#include "stats/fighter_sheet.hpp"
#include "stats/loading.hpp"

namespace fighter::ui {

FighterCard loadFighterCard(const std::filesystem::path& DataDir, const std::string& FileName) {
    FighterCard Card;
    Card.Name = FileName;
    Card.Weapon = "Unarmed";
    try {
        const stats::ItemCatalog Catalog = stats::loadItemCatalog(DataDir / "items");
        const stats::FighterSheet Sheet = stats::loadFighterSheet(DataDir / "fighters" / (FileName + ".json"));
        const stats::ResolvedFighter Fighter = stats::resolveFighterSheet(Sheet, Catalog);
        if (!Fighter.Name.empty()) Card.Name = Fighter.Name;
        Card.Strength = Fighter.BaseStats.Strength;
        Card.Dexterity = Fighter.BaseStats.Dexterity;
        Card.Constitution = Fighter.BaseStats.Constitution;
        for (const stats::EquipmentItem& Item : Fighter.Gear.Items) {
            if (Item.Slot == stats::EquipmentSlot::Weapon && Item.Weapon) Card.Weapon = Item.Name;
        }
    } catch (const std::exception& Error) {
        Card.Error = Error.what();
    }
    return Card;
}

} // namespace fighter::ui
