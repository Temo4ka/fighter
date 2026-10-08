//===- app/check_data.hpp - The --check-data command ------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares runDataCheck(), what `fighter_app --check-data` does:
/// checks every file of data/ (combat::checkData()) without opening a window
/// and prints the problems.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <iosfwd>

namespace fighter::app {

/// Checks \p Root / "data" and prints one line per problem ("file: detail")
/// and a summary to \p Out. Returns 0 if the data is sound, 1 otherwise.
int runDataCheck(const std::filesystem::path& Root, std::ostream& Out);

} // namespace fighter::app
