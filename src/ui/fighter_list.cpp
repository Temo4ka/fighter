#include "ui/fighter_list.hpp"

#include <algorithm>
#include <system_error>

namespace fighter::ui {

std::vector<std::string> listFighters(const std::filesystem::path& FightersDir) {
    std::vector<std::string> Names;
    std::error_code Error;
    for (const auto& Entry : std::filesystem::directory_iterator(FightersDir, Error)) {
        if (Entry.is_regular_file(Error) && Entry.path().extension() == ".json")
            Names.push_back(Entry.path().stem().string());
    }
    std::ranges::sort(Names);
    return Names;
}

} // namespace fighter::ui
