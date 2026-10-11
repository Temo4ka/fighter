#include "editor/new_clip.hpp"

#include <format>
#include <system_error>
#include <utility>

namespace fighter::editor {

std::string findNewClipProblem(const std::filesystem::path& DataDir, std::string_view Name) {
    if (Name.empty()) return "the name is empty";
    if (Name.size() > MaxClipNameLength) return std::format("the name is longer than {} characters", MaxClipNameLength);
    if (Name.front() < 'a' || Name.front() > 'z') return "the name must start with a letter a-z";
    for (const char Symbol : Name) {
        const bool Allowed = (Symbol >= 'a' && Symbol <= 'z') || (Symbol >= '0' && Symbol <= '9') || Symbol == '_';
        if (!Allowed) return "the name may have only a-z, 0-9 and _";
    }
    const std::filesystem::path Path = DataDir / "poses" / (std::string(Name) + ".json");
    std::error_code Error;
    if (std::filesystem::exists(Path, Error)) return std::format("a clip named {} already exists", Name);
    return {};
}

anim::Clip makeClipFromTemplate(const anim::Clip& Template, std::string Name) {
    anim::Clip Result = Template;
    Result.Name = std::move(Name);
    return Result;
}

} // namespace fighter::editor
