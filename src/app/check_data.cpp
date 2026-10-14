#include "app/check_data.hpp"

#include <cstddef>
#include <exception>
#include <format>
#include <ostream>
#include <string>
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
    size_t Errors = 0;
    for (const combat::DataProblem& Problem : Problems) {
        if (!Problem.Warning) ++Errors;
        Out << (Problem.Warning ? "warning: " : "") << Problem.format() << '\n';
    }
    const size_t Warnings = Problems.size() - Errors;
    const std::string WarningText =
        Warnings == 0 ? "" : std::format(", {} {}", Warnings, Warnings == 1 ? "warning" : "warnings");
    if (Errors == 0) {
        Out << "data check: " << DataDir.string() << " is sound" << WarningText << '\n';
        return 0;
    }
    Out << "data check: " << Errors << (Errors == 1 ? " problem" : " problems") << WarningText << " in "
        << DataDir.string() << '\n';
    return 1;
}

} // namespace fighter::app
