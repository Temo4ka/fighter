#include "app/check_data.hpp"

#include <exception>
#include <ostream>
#include <vector>

#include "combat/data_check.hpp"
#include "editor/editor_config.hpp"

namespace fighter::app {

int runDataCheck(const std::filesystem::path& Root, std::ostream& Out) {
    const std::filesystem::path DataDir = Root / "data";
    std::vector<combat::DataProblem> Problems = combat::checkData(DataDir);
    // The thresholds of the pose editor are not read by a battle, so the
    // check of the data of combat does not know them.
    try {
        editor::loadEditorConfigOrDefault(DataDir);
    } catch (const std::exception& Error) {
        Problems.push_back({.File = "editor.json", .Detail = Error.what()});
    }
    for (const combat::DataProblem& Problem : Problems) Out << Problem.format() << '\n';
    if (Problems.empty()) {
        Out << "data check: " << DataDir.string() << " is sound\n";
        return 0;
    }
    Out << "data check: " << Problems.size() << (Problems.size() == 1 ? " problem" : " problems") << " in "
        << DataDir.string() << '\n';
    return 1;
}

} // namespace fighter::app
