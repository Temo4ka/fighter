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
    for (const auto& Entry : Library.Clips) Library.addCopies(Entry.first);
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
            Clips.addCopies(*Name);
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

const anim::Clip& ClipLibrary::getOtherHand(const anim::Clip& Source) const {
    const auto Authored = Clips.find(Source.Name);
    if (Authored != Clips.end() && &Authored->second == &Source) {
        const auto Found = OtherHand.find(Source.Name);
        return Found == OtherHand.end() ? Source : Found->second;
    }
    for (const auto& [Name, Copy] : Mirrored) {
        if (&Copy != &Source) continue;
        const auto Found = MirroredOtherHand.find(Name);
        return Found == MirroredOtherHand.end() ? Source : Found->second;
    }
    return Source;
}

const anim::Clip& ClipLibrary::getAuthored(const anim::Clip& Source) const {
    for (const auto* Copies : {&Mirrored, &OtherHand, &MirroredOtherHand}) {
        for (const auto& [Name, Copy] : *Copies) {
            if (&Copy == &Source) return Clips.find(Name)->second;
        }
    }
    return Source;
}

bool ClipLibrary::isMirrored(const anim::Clip& Source) const { return &getAuthored(Source) != &Source; }

bool ClipLibrary::isOtherHand(const anim::Clip& Source) const {
    for (const auto* Copies : {&OtherHand, &MirroredOtherHand}) {
        for (const auto& Entry : *Copies) {
            if (&Entry.second == &Source) return true;
        }
    }
    return false;
}

void ClipLibrary::addCopies(const std::string& Name) {
    const anim::Clip& Authored = Clips.find(Name)->second;
    const bool Legs = anim::usesLegs(Authored);
    if (Legs) Mirrored.emplace(Name, anim::mirrorClipLegs(Authored));
    if (!anim::usesArms(Authored)) return;
    OtherHand.emplace(Name, anim::mirrorClipArms(Authored));
    if (Legs) MirroredOtherHand.emplace(Name, anim::mirrorClipArms(Mirrored.find(Name)->second));
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
