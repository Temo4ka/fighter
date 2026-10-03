#include "combat/clip_library.hpp"

#include <format>
#include <stdexcept>
#include <system_error>

namespace fighter::combat {

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
    const std::filesystem::path File = PosesDir / (std::string(Name) + ".json");
    std::error_code Error;
    if (!std::filesystem::exists(File, Error)) {
        throw std::runtime_error(std::format("{}: missing clip file", File.string()));
    }
    Clips.emplace(std::string(Name), anim::loadClip(File));
}

} // namespace fighter::combat
