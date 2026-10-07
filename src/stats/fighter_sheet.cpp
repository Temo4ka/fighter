#include "stats/fighter_sheet.hpp"

#include <format>

#include "stats/validation.hpp"

namespace fighter::stats {

ResolvedFighter resolveFighterSheet(const FighterSheet& Sheet, const ItemCatalog& Catalog) {
    return withErrorContext(std::format("fighter '{}'", Sheet.Name), [&] {
        validateStats(Sheet.BaseStats);
        return ResolvedFighter{
            .Name = Sheet.Name,
            .BaseStats = Sheet.BaseStats,
            .Gear = buildLoadout(Sheet.Items, Catalog),
        };
    });
}

} // namespace fighter::stats
