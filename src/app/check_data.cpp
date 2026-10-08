#include "app/check_data.hpp"

#include <ostream>
#include <vector>

#include "combat/data_check.hpp"

namespace fighter::app {

int runDataCheck(const std::filesystem::path& Root, std::ostream& Out) {
    const std::filesystem::path DataDir = Root / "data";
    const std::vector<combat::DataProblem> Problems = combat::checkData(DataDir);
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
