#include "combat/clip_library.hpp"

#include <format>
#include <stdexcept>
#include <system_error>

#include "anim/layers.hpp"

namespace fighter::combat {

ClipLibrary ClipLibrary::load(const std::filesystem::path& PosesDir, std::span<const MoveDef> Moves) {
    ClipLibrary Library;
    for (const std::string_view Name : {clips::Stance, clips::Walk, clips::Crouch, clips::CrouchWalk, clips::BlockHigh,
                                        clips::BlockMid, clips::BlockLow, clips::Flinch, clips::Stagger,
                                        clips::Knockback}) {
        Library.add(PosesDir, Name);
    }
    for (const MoveDef& Move : Moves) {
        Library.add(PosesDir, Move.Clip);
        if (!Move.CloseClip.empty()) Library.add(PosesDir, Move.CloseClip);
    }
    for (const auto& [Name, Clip] : Library.Clips) {
        if (anim::usesLegs(Clip)) Library.Mirrored.emplace(Name, anim::mirrorClipLegs(Clip));
    }
    return Library;
}

ClipLibrary ClipLibrary::load(const std::filesystem::path& PosesDir, const MoveLibrary& Library) {
    ClipLibrary Clips = load(PosesDir, Library.getMoves());
    for (const MoveSet& Set : Library.getSets()) {
        for (const std::optional<std::string>& Name : Set.Block.Clips) {
            if (!Name || Clips.Clips.contains(*Name)) continue;
            try {
                Clips.add(PosesDir, *Name);
            } catch (const std::exception& Error) {
                throw std::runtime_error(std::format("movesets/{}.json: block clip '{}': {}", Set.Id, *Name,
                                                     Error.what()));
            }
            const anim::Clip& Added = Clips.get(*Name);
            if (anim::usesLegs(Added)) Clips.Mirrored.emplace(*Name, anim::mirrorClipLegs(Added));
        }
    }
    return Clips;
}

const anim::Clip& ClipLibrary::get(std::string_view Name) const {
    const auto Found = Clips.find(Name);
    if (Found == Clips.end()) throw std::out_of_range(std::format("clip '{}' is not loaded", Name));
    return Found->second;
}

const anim::Clip* ClipLibrary::find(std::string_view Name) const {
    const auto Found = Clips.find(Name);
    return Found == Clips.end() ? nullptr : &Found->second;
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

const anim::Clip& ClipLibrary::getMirrored(const anim::Clip& Source) const {
    const auto Authored = Clips.find(Source.Name);
    if (Authored == Clips.end() || &Authored->second != &Source) return Source;
    const auto Found = Mirrored.find(Source.Name);
    return Found == Mirrored.end() ? Source : Found->second;
}

const anim::Clip& ClipLibrary::getAuthored(const anim::Clip& Source) const {
    for (const auto& [Name, Copy] : Mirrored) {
        if (&Copy == &Source) return Clips.find(Name)->second;
    }
    return Source;
}

bool ClipLibrary::isMirrored(const anim::Clip& Source) const { return &getAuthored(Source) != &Source; }

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
