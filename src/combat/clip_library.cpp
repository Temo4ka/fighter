#include "combat/clip_library.hpp"

#include <array>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "core/log.hpp"

namespace fighter::combat {
namespace {

/// A clip and the one that plays while it does not exist yet.
struct StandIn {
    std::string_view Name;
    std::string_view Replacement;
};

// PLACEHOLDER until task 2.2 (agent B): delete with findStandInClip().
constexpr std::array StandInTable = {
    StandIn{"heavy_punch", "jab"},       StandIn{"low_kick", "kick"},
    StandIn{"sword_slash", "jab"},       StandIn{"hammer_smash", "jab"},
    StandIn{clips::Crouch, clips::Stance},    StandIn{clips::BlockHigh, clips::Stance},
    StandIn{clips::BlockMid, clips::Stance},  StandIn{clips::BlockLow, clips::Stance},
    StandIn{clips::Flinch, clips::Stance},    StandIn{clips::Stagger, clips::Stance},
    StandIn{clips::Knockback, clips::Stance},
};

constexpr std::string_view CloseSuffix = "_close";
/// heavy_punch_close -> heavy_punch -> jab is the longest way.
constexpr int MaxStandInSteps = 4;

} // namespace

ClipLibrary ClipLibrary::load(const std::filesystem::path& PosesDir, std::span<const MoveDef> Moves) {
    ClipLibrary Library;
    for (const std::string_view Name : {clips::Stance, clips::Walk, clips::Crouch, clips::BlockHigh, clips::BlockMid,
                                        clips::BlockLow, clips::Flinch, clips::Stagger, clips::Knockback}) {
        Library.add(PosesDir, Name);
    }
    for (const MoveDef& Move : Moves) {
        Library.add(PosesDir, Move.Clip);
        if (!Move.CloseClip.empty()) Library.add(PosesDir, Move.CloseClip);
    }
    if (!Library.StandIns.empty()) {
        std::string List;
        for (const std::string& Entry : Library.StandIns) List += (List.empty() ? "" : ", ") + Entry;
        log::warn("{}: {} clips are missing, stand-ins play until they exist: {}", PosesDir.string(),
                  Library.StandIns.size(), List);
    }
    return Library;
}

const anim::Clip& ClipLibrary::get(std::string_view Name) const {
    const auto Found = Clips.find(Name);
    if (Found == Clips.end()) throw std::out_of_range(std::format("clip '{}' is not loaded", Name));
    return Found->second;
}

const anim::Clip& ClipLibrary::getBlock(BlockZone Zone) const {
    switch (Zone) {
        case BlockZone::High: return get(clips::BlockHigh);
        case BlockZone::Mid: return get(clips::BlockMid);
        case BlockZone::Low: return get(clips::BlockLow);
    }
    return get(clips::BlockMid);
}

const anim::Clip* ClipLibrary::findReaction(ReactionLevel Level) const {
    switch (Level) {
        case ReactionLevel::Flinch: return &get(clips::Flinch);
        case ReactionLevel::Stagger: return &get(clips::Stagger);
        case ReactionLevel::Knockback: return &get(clips::Knockback);
        default: return nullptr;
    }
}

void ClipLibrary::add(const std::filesystem::path& PosesDir, std::string_view Name) {
    if (Clips.contains(Name)) return;
    std::string Playing(Name);
    for (int Step = 0; Step <= MaxStandInSteps; ++Step) {
        const std::filesystem::path File = PosesDir / (Playing + ".json");
        std::error_code Error;
        if (std::filesystem::exists(File, Error)) {
            if (Playing != Name) StandIns.push_back(std::format("{} -> {}", Name, Playing));
            Clips.emplace(std::string(Name), anim::loadClip(File));
            return;
        }
        std::optional<std::string> Next = findStandInClip(Playing);
        if (!Next) break;
        Playing = std::move(*Next);
    }
    throw std::runtime_error(std::format("{}: missing clip file", (PosesDir / (std::string(Name) + ".json")).string()));
}

std::optional<std::string> findStandInClip(std::string_view Name) {
    if (Name.ends_with(CloseSuffix)) return std::string(Name.substr(0, Name.size() - CloseSuffix.size()));
    for (const StandIn& Entry : StandInTable) {
        if (Entry.Name == Name) return std::string(Entry.Replacement);
    }
    return std::nullopt;
}

} // namespace fighter::combat
